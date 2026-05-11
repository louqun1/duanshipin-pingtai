//go:build windows

package linkmicmix

import "syscall"

func processAlive(pid int) bool {
	if pid <= 0 {
		return false
	}

	handle, err := syscall.OpenProcess(syscall.PROCESS_QUERY_LIMITED_INFORMATION, false, uint32(pid))
	if err != nil {
		return false
	}
	defer syscall.CloseHandle(handle)

	var code uint32
	if err := syscall.GetExitCodeProcess(handle, &code); err != nil {
		return false
	}
	return code == syscall.STILL_ACTIVE
}

func killProcess(pid int) error {
	if pid <= 0 {
		return nil
	}

	handle, err := syscall.OpenProcess(syscall.PROCESS_TERMINATE|syscall.PROCESS_QUERY_LIMITED_INFORMATION, false, uint32(pid))
	if err != nil {
		if err == syscall.ERROR_INVALID_PARAMETER {
			return nil
		}
		return err
	}
	defer syscall.CloseHandle(handle)

	var code uint32
	if err := syscall.GetExitCodeProcess(handle, &code); err == nil && code != syscall.STILL_ACTIVE {
		return nil
	}

	return syscall.TerminateProcess(handle, 1)
}
