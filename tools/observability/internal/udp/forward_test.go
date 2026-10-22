package udp

import (
	"net"
	"strconv"
	"sync"
	"testing"
	"time"

	"tortoise-observability/internal/ringbuf"
	"tortoise-observability/internal/state"
)

// countingHub is read from the test goroutine while readLoop broadcasts, so it locks.
type countingHub struct {
	mu     sync.Mutex
	counts map[string]int
}

func (h *countingHub) Broadcast(event string, payload interface{}) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.counts[event]++
}

func (h *countingHub) count(event string) int {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.counts[event]
}

// serverInfoDatagram is processed with no side effects beyond one "server_info" broadcast.
const serverInfoDatagram = `{"v":12,"type":"SERVER_INFO"}`

func newForwardListener(t *testing.T, targets []string) (*Listener, *countingHub) {
	t.Helper()
	store := state.New(state.Config{RosterTTL: 30 * time.Second}, ringbuf.New(16))
	hub := &countingHub{counts: map[string]int{}}
	l := NewListener("127.0.0.1", 0, store, nil, nil, hub)
	if err := l.SetForwards(targets); err != nil {
		t.Fatalf("SetForwards: %v", err)
	}
	if err := l.Start(); err != nil {
		t.Fatalf("Start: %v", err)
	}
	t.Cleanup(l.Stop)
	return l, hub
}

func openTarget(t *testing.T) *net.UDPConn {
	t.Helper()
	c, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = c.Close() })
	return c
}

func listenAddr(l *Listener) *net.UDPAddr {
	return l.conn.LocalAddr().(*net.UDPAddr)
}

func sendDatagram(t *testing.T, to *net.UDPAddr, data []byte) {
	t.Helper()
	c, err := net.DialUDP("udp", nil, to)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if _, err := c.Write(data); err != nil {
		t.Fatal(err)
	}
}

func readOne(t *testing.T, c *net.UDPConn) []byte {
	t.Helper()
	if err := c.SetReadDeadline(time.Now().Add(2 * time.Second)); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 65535)
	n, _, err := c.ReadFromUDP(buf)
	if err != nil {
		t.Fatalf("target received nothing: %v", err)
	}
	return buf[:n]
}

func waitFor(t *testing.T, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for !cond() {
		if time.Now().After(deadline) {
			t.Fatalf("timed out waiting for %s", what)
		}
		time.Sleep(5 * time.Millisecond)
	}
}

func TestForwardSendsIdenticalBytesToEveryTarget(t *testing.T) {
	a := openTarget(t)
	b := openTarget(t)
	l, hub := newForwardListener(t, []string{a.LocalAddr().String(), b.LocalAddr().String()})

	sendDatagram(t, listenAddr(l), []byte(serverInfoDatagram))

	for _, c := range []*net.UDPConn{a, b} {
		if got := string(readOne(t, c)); got != serverInfoDatagram {
			t.Fatalf("forwarded bytes differ: %q", got)
		}
	}
	waitFor(t, "the normal processing of the datagram", func() bool { return hub.count("server_info") == 1 })
}

func TestDeadForwardTargetDoesNotBreakProcessing(t *testing.T) {
	dead := openTarget(t)
	deadAddr := dead.LocalAddr().String()
	_ = dead.Close()
	live := openTarget(t)
	l, hub := newForwardListener(t, []string{deadAddr, live.LocalAddr().String()})

	const n = 20
	start := time.Now()
	for i := 0; i < n; i++ {
		sendDatagram(t, listenAddr(l), []byte(serverInfoDatagram))
	}
	waitFor(t, "all datagrams processed", func() bool { return hub.count("server_info") == n })
	if elapsed := time.Since(start); elapsed > time.Second {
		t.Fatalf("20 datagrams with a dead target took %v", elapsed)
	}
	for i := 0; i < n; i++ {
		if got := string(readOne(t, live)); got != serverInfoDatagram {
			t.Fatalf("live target copy %d: %q", i, got)
		}
	}
}

func TestSetForwardsRejectsInvalidAddress(t *testing.T) {
	for _, bad := range []string{"", "no-port-here", "127.0.0.1", "127.0.0.1:0", "127.0.0.1:99999"} {
		store := state.New(state.Config{RosterTTL: 30 * time.Second}, ringbuf.New(16))
		l := NewListener("127.0.0.1", 0, store, nil, nil, &countingHub{counts: map[string]int{}})
		if err := l.SetForwards([]string{bad}); err == nil {
			t.Fatalf("forward target %q was accepted", bad)
		}
	}
}

func TestSetForwardsRefusesItsOwnListenAddress(t *testing.T) {
	probe := openTarget(t)
	port := probe.LocalAddr().(*net.UDPAddr).Port
	_ = probe.Close()

	store := state.New(state.Config{RosterTTL: 30 * time.Second}, ringbuf.New(16))
	l := NewListener("127.0.0.1", port, store, nil, nil, &countingHub{counts: map[string]int{}})
	if err := l.SetForwards([]string{"127.0.0.1:" + strconv.Itoa(port)}); err == nil {
		t.Fatal("a forward target equal to the listen address was accepted")
	}
}

func TestValidateForwards(t *testing.T) {
	cases := []struct {
		name    string
		listen  string
		targets []string
		refused bool
	}{
		{"no targets", "127.0.0.1:9195", nil, false},
		{"other port", "127.0.0.1:9195", []string{"127.0.0.1:9196"}, false},
		{"other ip same port", "127.0.0.1:9195", []string{"127.0.0.2:9195"}, false},
		{"remote host same port", "0.0.0.0:9195", []string{"10.0.0.5:9195"}, false},
		{"same address", "127.0.0.1:9195", []string{"127.0.0.1:9195"}, true},
		{"one of several", "127.0.0.1:9195", []string{"127.0.0.1:9196", "127.0.0.1:9195"}, true},
		{"wildcard listen, loopback target", "0.0.0.0:9195", []string{"127.0.0.1:9195"}, true},
		{"invalid target", "127.0.0.1:9195", []string{"nope"}, true},
		{"port zero target", "127.0.0.1:9195", []string{"127.0.0.1:0"}, true},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			err := validateForwards(tc.listen, tc.targets)
			if (err != nil) != tc.refused {
				t.Fatalf("validateForwards(%q, %v) = %v, refused want %v", tc.listen, tc.targets, err, tc.refused)
			}
		})
	}
}
