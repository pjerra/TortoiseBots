package main

import (
	"errors"
	"log"
	"net/http"
	"strconv"
	"strings"

	"tortoise-observability/internal/armory"
)

// model3dCharacterHandler serves the appearance payload for the optional v2 3D
// preview: race model id, saved customization choices and equipped display ids.
func model3dCharacterHandler(svc *armory.Service, enabled bool) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !enabled {
			http.NotFound(w, r)
			return
		}
		guid, err := strconv.ParseUint(r.PathValue("guid"), 10, 32)
		if err != nil {
			http.Error(w, "Invalid guid", http.StatusBadRequest)
			return
		}
		appearance, err := svc.ModelAppearance(uint32(guid))
		if err != nil {
			if errors.Is(err, armory.ErrModelUnsupported) || strings.Contains(err.Error(), "not found") ||
				strings.Contains(err.Error(), "has no model") {
				http.Error(w, err.Error(), http.StatusNotFound)
				return
			}
			log.Printf("[Model3D] guid=%d: %v", guid, err)
			http.Error(w, "appearance lookup failed", http.StatusInternalServerError)
			return
		}
		writeJSON(w, appearance)
	}
}
