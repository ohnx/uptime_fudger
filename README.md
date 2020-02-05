# uptime_fudger

Linux kernel module to fudge uptime.

This does not actually touch internal clocks but just adds an offset to common ways of fetching uptime.

## Usage

`make` this file, then `insmod` it. The default fudge factor is `0` (i.e., do not change anything).
You can modify it by passing the `fudge` argument to the module.

## How it works

### `/proc/uptime`

Used by: `uptime`

`uptime_fudger` will first unregister the default handler before registering its own. Upon unload, `uptime_fudger`
replaces the handler with itself.

### `sysinfo()`

Used by: (unknown, but it is a commonly suggested method on StackOverflow)

**planned, but not yet implemented**

### ???

Pull requests or issues are accepted! If you come across a bug or a program that doesn't seem to be affected by this,
please let me know :)




