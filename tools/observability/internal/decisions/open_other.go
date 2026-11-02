//go:build !unix

package decisions

import "os"

// openNoFollow is the non-unix fallback: there is no O_NOFOLLOW or O_NONBLOCK here (and no FIFO to block on). A symbolic link
// swapped in after the caller's Lstat is caught by the os.SameFile comparison between that Lstat and the opened descriptor.
func openNoFollow(path string) (*os.File, error) {
	return os.Open(path)
}
