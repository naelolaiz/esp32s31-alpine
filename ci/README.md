# CI

Planned (step 23): QEMU user-mode and `qemu-system-riscv32 -M virt` smoke tests
for each Alpine build, then hardware tests over UART on the board
(flash, reset, wait for the login prompt, run `uname -a`, `apk --version`, `free`).
