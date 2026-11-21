package main

import (
	"net"
	"testing"
)

func TestListenHTTPTakesNextFreePort(t *testing.T) {
	busy, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer busy.Close()
	taken := busy.Addr().(*net.TCPAddr).Port

	ln, port, err := listenHTTP("127.0.0.1", taken)
	if err != nil {
		t.Skipf("no free port above %d: %v", taken, err)
	}
	defer ln.Close()
	if port <= taken || port >= taken+httpPortSpan {
		t.Fatalf("port %d is busy: got %d, want one of the next %d", taken, port, httpPortSpan-1)
	}
}

func TestListenHTTPKeepsAFreePort(t *testing.T) {
	probe, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	free := probe.Addr().(*net.TCPAddr).Port
	probe.Close()

	ln, port, err := listenHTTP("127.0.0.1", free)
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	if port != free {
		t.Fatalf("free port %d: got %d", free, port)
	}
}
