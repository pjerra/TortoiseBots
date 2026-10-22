package udp

import (
	"fmt"
	"log"
	"net"
	"time"
)

// forwardWriteTimeout bounds one forwarded write, so a slow or dead target
// cannot hold up the read loop.
const forwardWriteTimeout = 50 * time.Millisecond

// forwardLogInterval is the least time between two error lines for one target.
const forwardLogInterval = 30 * time.Second

// forwardTarget is one extra destination for every received datagram. Only the
// read loop touches lastLog, so it needs no lock.
type forwardTarget struct {
	addr    string
	conn    *net.UDPConn
	lastLog time.Time
}

// SetForwards sets the extra UDP addresses that every received datagram is
// copied to. Call it before Start; an empty list turns forwarding off.
func (l *Listener) SetForwards(addrs []string) error {
	if err := validateForwards(l.addr, addrs); err != nil {
		return err
	}

	var targets []*forwardTarget
	for _, a := range addrs {
		raddr, err := net.ResolveUDPAddr("udp", a)
		if err != nil {
			closeTargets(targets)
			return fmt.Errorf("forward target %s: %w", a, err)
		}
		conn, err := net.DialUDP("udp", nil, raddr)
		if err != nil {
			closeTargets(targets)
			return fmt.Errorf("forward target %s: %w", a, err)
		}
		targets = append(targets, &forwardTarget{addr: a, conn: conn})
	}
	l.forwards = targets
	return nil
}

// validateForwards refuses a target that is the listen address itself, or a
// loopback address on the same port when listening on the wildcard. Such a
// target would feed the daemon's output back into the socket it reads from.
func validateForwards(listen string, targets []string) error {
	listenAddr, err := net.ResolveUDPAddr("udp", listen)
	if err != nil {
		return fmt.Errorf("listen address %s: %w", listen, err)
	}
	for _, target := range targets {
		addr, err := net.ResolveUDPAddr("udp", target)
		if err != nil {
			return fmt.Errorf("forward target %s: %w", target, err)
		}
		if addr.Port == 0 {
			return fmt.Errorf("forward target %s: port must be 1-65535", target)
		}
		sameHost := addr.IP.Equal(listenAddr.IP) || (listenAddr.IP.IsUnspecified() && addr.IP.IsLoopback())
		if addr.Port == listenAddr.Port && sameHost {
			return fmt.Errorf("forward target %s is the listen address %s: telemetry would loop back into the daemon", target, listen)
		}
	}
	return nil
}

// forwardDatagram copies one received datagram to every forward target. A failed
// write is logged at most once per forwardLogInterval per target and never
// reaches the read loop, so processing always follows.
func (l *Listener) forwardDatagram(data []byte) {
	for _, t := range l.forwards {
		_ = t.conn.SetWriteDeadline(time.Now().Add(forwardWriteTimeout))
		if _, err := t.conn.Write(data); err != nil {
			if now := time.Now(); now.Sub(t.lastLog) >= forwardLogInterval {
				t.lastLog = now
				log.Printf("[UDP] Forward to %s failed: %v (repeats are silent for %v)", t.addr, err, forwardLogInterval)
			}
		}
	}
}

func (l *Listener) closeForwards() {
	closeTargets(l.forwards)
}

func closeTargets(targets []*forwardTarget) {
	for _, t := range targets {
		_ = t.conn.Close()
	}
}
