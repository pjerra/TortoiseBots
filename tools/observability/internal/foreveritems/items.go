// Package foreveritems reads the item table the TortoiseBots module writes on ForeverCore
// (TortoiseBots.Dashboard.ItemsFile). ForeverCore keeps item data in the client DB2 stores, not in
// a world table, so this file is the dashboard's item_template there.
package foreveritems

import (
	"encoding/json"
	"fmt"
	"os"
	"sync"
	"time"
)

type Item struct {
	ID       uint32 `json:"id"`
	Name     string `json:"name"`
	Quality  uint32 `json:"quality"`
	ItemLvl  uint32 `json:"ilvl"`
	ReqLevel uint32 `json:"req_level"`
	Class    uint32 `json:"class"`
	SubClass uint32 `json:"subclass"`
	InvType  uint32 `json:"inv_type"`
	Sell     uint32 `json:"sell"`
	Buy      uint32 `json:"buy"`
	Icon     uint32 `json:"icon"`
}

type Store struct {
	path    string
	mu      sync.RWMutex
	items   map[uint32]Item
	modTime time.Time
}

func New(path string) *Store { return &Store{path: path} }

// Get returns the item, reloading the file when it changed (a server restart rewrites it).
func (s *Store) Get(id uint32) (Item, bool) {
	s.refresh()
	s.mu.RLock()
	defer s.mu.RUnlock()
	it, ok := s.items[id]
	return it, ok
}

func (s *Store) Len() int {
	s.refresh()
	s.mu.RLock()
	defer s.mu.RUnlock()
	return len(s.items)
}

func (s *Store) refresh() {
	if s == nil || s.path == "" {
		return
	}
	st, err := os.Stat(s.path)
	if err != nil {
		return
	}
	s.mu.RLock()
	same := st.ModTime().Equal(s.modTime)
	s.mu.RUnlock()
	if same {
		return
	}
	items, err := load(s.path)
	if err != nil {
		return
	}
	s.mu.Lock()
	s.items, s.modTime = items, st.ModTime()
	s.mu.Unlock()
}

func load(path string) (map[uint32]Item, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var doc struct {
		Items []Item `json:"items"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		return nil, fmt.Errorf("items file %s: %w", path, err)
	}
	out := make(map[uint32]Item, len(doc.Items))
	for _, it := range doc.Items {
		out[it.ID] = it
	}
	return out, nil
}
