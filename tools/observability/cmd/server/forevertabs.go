package main

import (
	"net/http"
)

// corePorted says which parts of the dashboard the game core feeds. The Turtle module feeds everything; ForeverCore (the port) does not
// yet: its raid logic, the Commander orders and the commander card's live combat window are not ported. The pages read this once and say so
// on the tabs it concerns instead of showing an empty list that looks like "nothing is happening".
type corePorted struct {
	Raids           bool `json:"raids"`            // raid groups run by the module's raid logic (the Raids tab's fights and bosses)
	Coverage        bool `json:"coverage"`         // the boss tactic rows the bots act on (the Raid coverage tab)
	CommanderOrders bool `json:"commander_orders"` // orders sent from a commander (the Commanders tab's order log and buttons)
	Combat          bool `json:"combat"`           // the live combat window on the Commanders card
}

type coreView struct {
	Core   string     `json:"core"`
	Ported corePorted `json:"ported"`
}

// coreInfo answers GET /api/v1/core for the daemon's -core flavour.
func coreInfo(core string) coreView {
	if core == "forever" {
		return coreView{Core: "forever"}
	}
	return coreView{Core: "turtle", Ported: corePorted{Raids: true, Coverage: true, CommanderOrders: true, Combat: true}}
}

func coreHandler(core string) http.HandlerFunc {
	view := coreInfo(core)
	return func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, view)
	}
}
