package main

import (
	"bytes"
	"net/http"
	"strings"
)

// uiChoice is the page-facing view of the -ui and -ui-3d flags. The page also
// receives both as meta tags, so it can pick its skin before the first paint;
// this route exists for callers that only speak JSON.
type uiChoice struct {
	Default string `json:"default"`
	Model3D bool   `json:"model3d"`
}

func normalizeUI(v string) string {
	return strings.ToLower(strings.TrimSpace(v))
}

// injectUIMeta puts the server's skin default into the page head, before the
// stylesheet and the switch script, so uiv2.js reads it synchronously and the
// first paint already uses the right skin.
func injectUIMeta(page []byte, ui string, model3d bool) []byte {
	if bytes.Contains(page, []byte(`<meta name="tbdash-ui"`)) {
		return page
	}
	model := "0"
	if model3d {
		model = "1"
	}
	metas := []byte("<head>\n  <meta name=\"tbdash-ui\" content=\"" + ui + "\">\n" +
		"  <meta name=\"tbdash-ui-3d\" content=\"" + model + "\">")
	return bytes.Replace(page, []byte("<head>"), metas, 1)
}

func uiInfoHandler(ui string, model3d bool) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, uiChoice{Default: ui, Model3D: model3d})
	}
}
