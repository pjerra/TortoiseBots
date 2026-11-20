package plans

import (
	"context"
	"database/sql"
	"database/sql/driver"
	"io"
	"strings"
	"sync"
	"testing"

	"github.com/go-sql-driver/mysql"
)

// tcDriver is a tiny database/sql driver standing in for a world database. With tc set it is a TrinityCore schema (ForeverCore): the two
// creature reads that name the Turtle columns (level_max, detection_range, rank, movement_type) fail with MySQL's 1054, and the TC spelling
// (Classification, MovementType) answers. Without it the Turtle spelling answers and the TC one is never expected.
type tcDriver struct {
	mu      sync.Mutex
	tc      bool
	queries []string
}

type tcConn struct{ d *tcDriver }

func (c tcConn) Prepare(string) (driver.Stmt, error) { return nil, driver.ErrSkip }
func (c tcConn) Close() error                        { return nil }
func (c tcConn) Begin() (driver.Tx, error)           { return nil, driver.ErrSkip }

type tcRows struct {
	cols []string
	rows [][]driver.Value
	i    int
}

func (r *tcRows) Columns() []string { return r.cols }
func (r *tcRows) Close() error      { return nil }
func (r *tcRows) Next(dest []driver.Value) error {
	if r.i >= len(r.rows) {
		return io.EOF
	}
	copy(dest, r.rows[r.i])
	r.i++
	return nil
}

func (c tcConn) QueryContext(_ context.Context, query string, _ []driver.NamedValue) (driver.Rows, error) {
	c.d.mu.Lock()
	defer c.d.mu.Unlock()
	c.d.queries = append(c.d.queries, query)
	switch {
	case strings.Contains(query, "FROM `ai_dungeon_plan`"):
		return &tcRows{cols: []string{"plan", "map", "source", "comment"}, rows: [][]driver.Value{{"ragefire_chasm", int64(389), "test", ""}}}, nil
	case strings.Contains(query, "FROM `ai_dungeon_leg`"):
		return &tcRows{cols: []string{"leg", "name"}, rows: [][]driver.Value{{int64(0), "entrance"}}}, nil
	case strings.Contains(query, "FROM `ai_dungeon_step`"):
		return &tcRows{cols: []string{"leg", "seq", "kind", "entry", "x", "y", "z", "radius", "entries", "comment"}, rows: [][]driver.Value{
			{int64(0), int64(1), "move", int64(0), float64(1), float64(2), float64(3), float64(0), "", ""},
			{int64(0), int64(2), "pull", int64(0), float64(10), float64(20), float64(0), float64(12), "11321", "first pack"},
			{int64(0), int64(3), "boss", int64(11520), float64(30), float64(40), float64(0), float64(30), "", ""},
		}}, nil
	case strings.Contains(query, "FROM `creature_template`"):
		if strings.Contains(query, "`level_max`") {
			if c.d.tc {
				return nil, &mysql.MySQLError{Number: 1054, Message: "Unknown column 'level_max' in 'field list'"}
			}
			return &tcRows{cols: []string{"entry", "name", "level_max", "detection_range", "rank", "flags_extra"}, rows: [][]driver.Value{
				{int64(11321), "Searing Blade Cultist", int64(14), float64(18), int64(1), int64(0)},
				{int64(11520), "Taragaman the Hungerer", int64(16), float64(25), int64(3), int64(0)},
			}}, nil
		}
		if !c.d.tc || !strings.Contains(query, "`Classification`") {
			return nil, &mysql.MySQLError{Number: 1054, Message: "Unknown column in 'field list'"}
		}
		// the two literal 0 columns come back as integers, as MySQL sends them
		return &tcRows{cols: []string{"entry", "name", "0", "0", "Classification", "flags_extra"}, rows: [][]driver.Value{
			{int64(11321), "Searing Blade Cultist", int64(0), int64(0), int64(1), int64(0)},
			{int64(11520), "Taragaman the Hungerer", int64(0), int64(0), int64(3), int64(0)},
		}}, nil
	case strings.Contains(query, "FROM `creature`"):
		if strings.Contains(query, "`movement_type`") {
			if c.d.tc {
				return nil, &mysql.MySQLError{Number: 1054, Message: "Unknown column 'movement_type' in 'field list'"}
			}
			return &tcRows{cols: []string{"guid", "id", "position_x", "position_y", "position_z", "movement_type"}, rows: [][]driver.Value{
				{int64(5001), int64(11321), float64(11), float64(21), float64(0), int64(0)},
			}}, nil
		}
		if !c.d.tc || !strings.Contains(query, "`MovementType`") {
			return nil, &mysql.MySQLError{Number: 1054, Message: "Unknown column in 'field list'"}
		}
		return &tcRows{cols: []string{"guid", "id", "position_x", "position_y", "position_z", "MovementType"}, rows: [][]driver.Value{
			{int64(5001), int64(11321), float64(11), float64(21), float64(0), int64(2)},
		}}, nil
	}
	return nil, &mysql.MySQLError{Number: 1146, Message: "Table doesn't exist"}
}

func (d *tcDriver) Open(string) (driver.Conn, error) { return tcConn{d}, nil }

var tcDriverSeq int

func openTCDB(t *testing.T, tc bool) (*sql.DB, *tcDriver) {
	t.Helper()
	d := &tcDriver{tc: tc}
	tcDriverSeq++
	name := "plans-tc-test-" + string(rune('a'+tcDriverSeq))
	sql.Register(name, d)
	db, err := sql.Open(name, "")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })
	return db, d
}

func countQueries(d *tcDriver, sub string) int {
	d.mu.Lock()
	defer d.mu.Unlock()
	n := 0
	for _, q := range d.queries {
		if strings.Contains(q, sub) {
			n++
		}
	}
	return n
}

// TestTCSchemaFallback: a world database that answers MySQL 1054 to the Turtle creature columns (ForeverCore, a TrinityCore schema) gets the
// TC spelling: the rank comes from `Classification`, the patrol flag from `creature`.`MovementType`, level and detection are 0 (unknown), and
// from then on each read is ONE query (the first 1054 is remembered).
func TestTCSchemaFallback(t *testing.T) {
	db, drv := openTCDB(t, true)
	src := NewSQLSource(db)
	p, err := Build(context.Background(), src, "ragefire_chasm")
	if err != nil {
		t.Fatalf("Build on a TC schema: %v", err)
	}
	if len(p.Steps) != 3 || p.Steps[2].N != "Taragaman the Hungerer" || p.Steps[1].N != "Searing Blade Cultist" {
		t.Fatalf("steps = %+v", p.Steps)
	}
	if len(p.Mobs) != 1 {
		t.Fatalf("mobs = %+v", p.Mobs)
	}
	m := p.Mobs[0]
	if m.E != 11321 || m.N != "Searing Blade Cultist" || m.Rank != 1 || !m.Patrol || m.Lvl != 0 || m.Det != 0 {
		t.Fatalf("mob = %+v, want rank 1 from Classification, patrol from MovementType, level and detection 0", m)
	}
	if countQueries(drv, "`level_max`") != 1 || countQueries(drv, "`Classification`") != 1 {
		t.Fatalf("first read: %d Turtle template queries and %d TC ones, want 1 and 1", countQueries(drv, "`level_max`"), countQueries(drv, "`Classification`"))
	}
	// the template read already learned the schema, so the spawn read goes straight to the TC spelling
	if countQueries(drv, "`movement_type`") != 0 || countQueries(drv, "`MovementType`") != 1 {
		t.Fatalf("first read: spawn queries %v", drv.queries)
	}
	// A second plan read: straight to the TC queries, the Turtle ones are not tried again.
	if _, err := Build(context.Background(), src, "ragefire_chasm"); err != nil {
		t.Fatal(err)
	}
	if countQueries(drv, "`level_max`") != 1 || countQueries(drv, "`movement_type`") != 0 {
		t.Fatalf("the Turtle columns were tried again: %v", drv.queries)
	}
	if countQueries(drv, "`Classification`") != 2 || countQueries(drv, "`MovementType`") != 2 {
		t.Fatalf("the TC reads of the second plan: %v", drv.queries)
	}
}

// TestTurtleSchemaKeepsItsQueries: a Turtle database answers the first query, and the TC spelling is never sent.
func TestTurtleSchemaKeepsItsQueries(t *testing.T) {
	db, drv := openTCDB(t, false)
	p, err := Build(context.Background(), NewSQLSource(db), "ragefire_chasm")
	if err != nil {
		t.Fatalf("Build on a Turtle schema: %v", err)
	}
	if len(p.Mobs) != 1 || p.Mobs[0].Lvl != 14 || p.Mobs[0].Det != 18 || p.Mobs[0].Patrol {
		t.Fatalf("mobs = %+v", p.Mobs)
	}
	if countQueries(drv, "`Classification`") != 0 || countQueries(drv, "`MovementType`") != 0 {
		t.Fatalf("the TC spelling was sent to a Turtle database: %v", drv.queries)
	}
}

// TestOtherErrorsAreNotRetried: only MySQL 1054 switches to the TC spelling; a missing table (1146) is an error as before.
func TestOtherErrorsAreNotRetried(t *testing.T) {
	if isUnknownColumn(&mysql.MySQLError{Number: 1146}) || isUnknownColumn(io.EOF) || isUnknownColumn(nil) {
		t.Fatal("isUnknownColumn answers true for an error that is not 1054")
	}
	if !isUnknownColumn(&mysql.MySQLError{Number: 1054}) {
		t.Fatal("isUnknownColumn misses 1054")
	}
	for _, q := range []string{QueryTemplatesTC, QuerySpawnsTC} {
		if strings.Contains(q, "'") {
			t.Fatalf("query contains a quoted literal: %s", q)
		}
	}
}
