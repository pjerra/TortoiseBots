package auth

import (
	"crypto/hmac"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/hex"
	"fmt"
	"math/big"
	"strings"
	"time"
)

// ForeverCore (TrinityCore master) logins are Battle.net accounts: an e-mail and an SRP6 v2 salt and
// verifier in auth.battlenet_accounts (BnetSRP6v2, SRP6.cpp). The GM rank is the highest
// account_access.SecurityLevel of the game accounts linked to it.

var foreverBnetN, _ = new(big.Int).SetString("AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050A37329CBB4A099ED8193E0757767A13DD52312AB4B03310DCD7F48A9DA04FD50E8083969EDB767B0CF6095179A163AB3661A05FBD5FAAAE82918A9962F0B93B855F97993EC975EEAA80D740ADBF4FF747359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A436C6481F1D2B9078717461A5B9D32E688F87748544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB3786160279004E57AE6AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DBFBB694B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F9E4AFF73", 16)

const foreverBnetIterations = 15000

func pbkdf2SHA512(password, salt []byte, iterations int) []byte {
	mac := hmac.New(sha512.New, password)
	mac.Write(salt)
	mac.Write([]byte{0, 0, 0, 1})
	u := mac.Sum(nil)
	out := append([]byte(nil), u...)
	for i := 1; i < iterations; i++ {
		mac.Reset()
		mac.Write(u)
		u = mac.Sum(u[:0])
		for j := range out {
			out[j] ^= u[j]
		}
	}
	return out
}

// foreverVerifier is the stored verifier (little-endian, as the core writes it) for an e-mail and password.
func foreverVerifier(email, password string, salt []byte) []byte {
	digest := sha256.Sum256([]byte(strings.ToUpper(email)))
	srpUser := strings.ToUpper(hex.EncodeToString(digest[:]))
	xb := pbkdf2SHA512([]byte(srpUser+":"+password), salt, foreverBnetIterations)
	x := new(big.Int).SetBytes(xb)
	if xb[0]&0x80 != 0 {
		x.Sub(x, new(big.Int).Lsh(big.NewInt(1), 512))
	}
	x.Mod(x, new(big.Int).Sub(foreverBnetN, big.NewInt(1)))
	v := new(big.Int).Exp(big.NewInt(2), x, foreverBnetN).Bytes()
	for i, j := 0, len(v)-1; i < j; i, j = i+1, j-1 {
		v[i], v[j] = v[j], v[i]
	}
	return v
}

func trimZeros(b []byte) []byte {
	for len(b) > 0 && b[len(b)-1] == 0 {
		b = b[:len(b)-1]
	}
	return b
}

func (s *Service) authenticateForever(email, password string) (*SessionData, error) {
	var id uint32
	var srpVersion int
	var salt, verifier []byte
	err := s.db.QueryRow(`SELECT id, srp_version, salt, verifier FROM battlenet_accounts WHERE email = UPPER(?) LIMIT 1`, email).
		Scan(&id, &srpVersion, &salt, &verifier)
	if err != nil || srpVersion != 2 {
		return nil, fmt.Errorf("invalid username or password")
	}
	if !hmac.Equal(trimZeros(foreverVerifier(email, password, salt)), trimZeros(verifier)) {
		return nil, fmt.Errorf("invalid username or password")
	}
	rank, err := s.foreverRank(id)
	if err != nil || rank < s.minLevel() {
		return nil, fmt.Errorf("invalid username or password")
	}
	return &SessionData{
		AccountID: id,
		Username:  strings.ToUpper(email),
		Rank:      rank,
		ExpiresAt: time.Now().Add(24 * time.Hour).Unix(),
	}, nil
}

func (s *Service) foreverRank(bnetID uint32) (uint32, error) {
	var rank uint32
	err := s.db.QueryRow(`SELECT COALESCE(MAX(aa.SecurityLevel), 0) FROM account a JOIN account_access aa ON aa.AccountID = a.id WHERE a.battlenet_account = ?`, bnetID).Scan(&rank)
	return rank, err
}
