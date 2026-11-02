//go:build unix

package decisions

import (
	"os"
	"syscall"
)

// openNoFollow opens a file for reading without following a symbolic link in the final path component, and without blocking on a
// FIFO that may have replaced the file after the caller's check. Opening a regular file with O_NONBLOCK has no effect.
func openNoFollow(path string) (*os.File, error) {
	return os.OpenFile(path, os.O_RDONLY|syscall.O_NOFOLLOW|syscall.O_NONBLOCK, 0)
}
