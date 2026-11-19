package auth

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"testing"
	"time"
)

const testSecret = "test-secret-key"

func newTestService(t *testing.T) *Service {
	t.Helper()
	s, err := NewService(Config{SecretKey: testSecret, MinGMLevel: 2})
	if err != nil {
		t.Fatalf("NewService: %v", err)
	}
	return s
}

func TestNewServiceRequiresSecretKey(t *testing.T) {
	if _, err := NewService(Config{}); err == nil {
		t.Fatal("NewService with an empty SecretKey must fail, not fall back to a shipped constant")
	}
}

func TestSessionTokenRoundTrip(t *testing.T) {
	s := newTestService(t)
	want := &SessionData{AccountID: 42, Username: "GMAGE", Rank: 3, ExpiresAt: time.Now().Add(time.Hour).Unix()}

	got, err := s.VerifySessionToken(s.CreateSessionToken(want))
	if err != nil {
		t.Fatalf("VerifySessionToken: %v", err)
	}
	if got != nil && (*got != *want) {
		t.Errorf("round trip changed the session: got %+v, want %+v", *got, *want)
	}
}

func TestSessionTokenExpiry(t *testing.T) {
	s := newTestService(t)
	expired := &SessionData{AccountID: 1, Username: "GMAGE", Rank: 2, ExpiresAt: time.Now().Add(-time.Second).Unix()}

	if _, err := s.VerifySessionToken(s.CreateSessionToken(expired)); err == nil {
		t.Fatal("an expired token must be rejected")
	}
}

func TestSessionTokenTamperRejected(t *testing.T) {
	s := newTestService(t)
	token := s.CreateSessionToken(&SessionData{AccountID: 1, Username: "GMAGE", Rank: 2, ExpiresAt: time.Now().Add(time.Hour).Unix()})

	// Escalate the rank in the signed payload.
	forged := fmt.Sprintf("1:GMAGE:5:%s", token[len("1:GMAGE:2:"):])
	if _, err := s.VerifySessionToken(forged); err == nil {
		t.Fatal("a token whose payload was modified must be rejected")
	}
}

// The constants the issue flagged (main.go and the old auth fallback) must
// no longer open a session: keys are per-install randoms now.
func TestForgedTokenWithOldConstantKeyRejected(t *testing.T) {
	s := newTestService(t)
	payload := "1:GMAGE:3:9999999999"
	for _, oldKey := range []string{"tortoise-observability-salt-secret", "tortoise-observability-secret-salt"} {
		mac := hmac.New(sha256.New, []byte(oldKey))
		mac.Write([]byte(payload))
		token := fmt.Sprintf("%s.%s", payload, hex.EncodeToString(mac.Sum(nil)))
		if _, err := s.VerifySessionToken(token); err == nil {
			t.Errorf("a token forged with the retired constant key %q must be rejected", oldKey)
		}
	}
}

func TestLoginBackoff(t *testing.T) {
	now := time.Unix(1000000, 0)
	b := NewLoginBackoff()
	b.now = func() time.Time { return now }

	if !b.Allow("10.0.0.1") {
		t.Fatal("a fresh IP must be allowed")
	}

	// maxFailures-1 failures do not block.
	for range b.maxFailures - 1 {
		b.Fail("10.0.0.1")
	}
	if !b.Allow("10.0.0.1") {
		t.Fatal("below the failure limit the IP must still be allowed")
	}

	// The limit-th failure blocks for blockDuration.
	b.Fail("10.0.0.1")
	if b.Allow("10.0.0.1") {
		t.Fatal("an IP at the failure limit must be blocked")
	}

	// After the block window it is allowed again.
	now = now.Add(b.blockDuration + time.Second)
	if !b.Allow("10.0.0.1") {
		t.Fatal("after the block window the IP must be allowed again")
	}

	// A successful login forgets the failures.
	other := now
	b.now = func() time.Time { return other }
	b.Fail("10.0.0.2")
	b.Fail("10.0.0.2")
	b.Reset("10.0.0.2")
	if !b.Allow("10.0.0.2") {
		t.Fatal("Reset must lift the failures of an IP")
	}

	// Failures older than the block window stop counting: four stale
	// failures do not push a fresh burst over the limit.
	b.Fail("10.0.0.3")
	b.Fail("10.0.0.3")
	b.Fail("10.0.0.3")
	b.Fail("10.0.0.3")
	stale := other.Add(b.blockDuration + time.Second)
	b.now = func() time.Time { return stale }
	b.Fail("10.0.0.3")
	if !b.Allow("10.0.0.3") {
		t.Fatal("failures older than the window must stop counting")
	}

	// The limit still applies to a fresh burst: the one above plus four
	// more reaches it and blocks.
	for range b.maxFailures - 1 {
		b.Fail("10.0.0.3")
	}
	if b.Allow("10.0.0.3") {
		t.Fatal("an IP at the failure limit must be blocked")
	}
	fresh := stale.Add(b.blockDuration + time.Second)
	b.now = func() time.Time { return fresh }
	if !b.Allow("10.0.0.3") {
		t.Fatal("after the window the IP must be allowed again")
	}
}

func TestLoginBackoffPrune(t *testing.T) {
	now := time.Unix(1000000, 0)
	b := NewLoginBackoff()
	b.maxEntries = 2
	b.now = func() time.Time { return now }

	// Stale entries are evicted instead of growing the map without bound.
	old := now.Add(-2 * time.Hour)
	b.now = func() time.Time { return old }
	b.Fail("10.0.0.1")
	b.Fail("10.0.0.2")
	b.now = func() time.Time { return now }
	b.Fail("10.0.0.3")
	b.Fail("10.0.0.4")

	if len(b.failures) > b.maxEntries {
		t.Errorf("backoff map grew past maxEntries: %d entries", len(b.failures))
	}
	if _, ok := b.failures["10.0.0.1"]; ok {
		t.Error("a two-hour-stale entry must have been pruned")
	}
	if _, ok := b.failures["10.0.0.4"]; !ok {
		t.Error("the fresh entry must survive the prune")
	}
}

func TestLoginBackoffHardCap(t *testing.T) {
	now := time.Unix(1000000, 0)
	b := NewLoginBackoff()
	b.maxEntries = 2
	b.now = func() time.Time { return now }

	// Fill the map with fresh failures; pruning frees nothing.
	b.Fail("10.0.0.1")
	b.Fail("10.0.0.2")

	// A further address is refused instead of evicting a live entry: the
	// map stays bounded even under an address flood, and the untracked
	// address is not blocked (it simply is not counted).
	b.Fail("10.0.0.3")
	if len(b.failures) != b.maxEntries {
		t.Errorf("backoff map grew past maxEntries: %d entries", len(b.failures))
	}
	if _, ok := b.failures["10.0.0.3"]; ok {
		t.Error("a new address must not be tracked while the map is full")
	}
	if !b.Allow("10.0.0.3") {
		t.Error("an untracked address must not be blocked")
	}

	// Once room frees up (a window elapsed), new addresses are tracked again.
	later := now.Add(b.blockDuration + time.Second)
	b.now = func() time.Time { return later }
	b.Fail("10.0.0.3")
	if _, ok := b.failures["10.0.0.3"]; !ok {
		t.Error("a new address must be tracked once room has freed up")
	}
}

func TestForeverVerifier(t *testing.T) {
	salt, err := hex.DecodeString("D05CA43A1B53349840827652DC55D432BEEE8828D18127135528B015CE76168B")
	if err != nil {
		t.Fatalf("decode salt: %v", err)
	}
	want, err := hex.DecodeString("7266E020147EDD082F30B27A5D0918E71777672471EFBA1D58E2282E4B886A72A8C68B45DC26D694CAD04861CD65DB237DCAC8102375462DF3B2CC718F2630599751C0AEC784B31FDD6B20DC823600C4BB03E9DB543A1914B54DDE8016DFAAD316893A2D737D166E63E19E82555B719540DB6301937BCAE06B70C993086257A8024168D3CE10CB585E537E6365ACC8E2A0E52DE0786A4E914DF2A9DC72F3762403AEA111591AE6357349BB1B75C6C5BBB31E86916B1B4FE5A39B178EBB17A214E0A7BFD6E923345B213A941D7DF91C70D82CF63664B26FEE139D915A1BE9AE5D98BA2AEFB414EC8C2ED147A8CA2E3E632187C8CCF733BBD89E786677C3F5DE7B")
	if err != nil {
		t.Fatalf("decode verifier: %v", err)
	}

	if got := foreverVerifier("admin@forever", "admin", salt); !hmac.Equal(trimZeros(got), trimZeros(want)) {
		t.Errorf("foreverVerifier(admin@forever, admin) did not match stored verifier")
	}
	if got := foreverVerifier("ADMIN@FOREVER", "ADMIN", salt); hmac.Equal(trimZeros(got), trimZeros(want)) {
		t.Errorf("foreverVerifier(ADMIN@FOREVER, ADMIN) must not match (password is case-sensitive)")
	}
}
