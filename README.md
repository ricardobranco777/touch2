# touch2

You can use touch(1) to change the last-access & last-modification times. Use touch2 to change the last-change time.

It must be run as root or with *CAP_SYS_TIME* capabilities.

## BUGS / LIMITATIONS

- \*BSD systems restrict settimeofday(2) when running in secure mode
- Nanosecond resolution it's impossible to get right
