# RP2350 Powman States

## Description

An example of using the new powman states available in the RP2350 to go to a
lower power sleep (<500 uA) compared to the `SLEEP` and `DORMANT` states of the
RP2040.

This uses the powman BOOT registers to save the current execution state before
powering down the core. On power-up, it resumes execution as normal.

Without using the powman BOOT registers, changing to a lower power state will
reboot the core and run from the start of `main()` again, which isn't useful
for most applications that want sleep.

It crucially leaves the SRAM domains on to retain any global state and stack
contents.

Example is built for Arm but can be adapted to run on RISC-V cores.

## Explanation

I'd like to preface this information with the fact that I'm not a microcontroller
expert, nor have I used the RP2350 an awful lot but I have dug into a reset
sequence or two.

To understand why powman sleep is done the way it is, we need to first look at
the power states provided by the RP2350's predecessor, the RP2040.
These states were called `SLEEP` and `DORMANT` and worked by stopping the clocks
that ran the cores while retaining the processor state like registers etc.
These states are still present on the RP2350 as well.
They reduced power by a lot, but the processor was still powered on, so the
idle power draw/leakage current still drew 1-2 mA.
Not so great when other microcontrollers (like some STM32 series) can go to
sub-100 uA range in sleep modes.

So how does Raspberry Pi fix this problem? Well its simple: If you aren't using
the processor, then lets just turn it all off!
Well, technically it turns off the switched core and peripherals, and retains a
small section of always-on (AON) circuitry, which contains the powman peripheral.
You can optionally power-down the RAM, but if you want to retain information
during sleep, you probably won't want to do that, or risk wearing out your flash
by constantly writing your program's state to it before sleeping.

Turning off the core results in a drastic power drop of around 10x, down as low
as 150 uA!
This is excellent! There's only one problem: We've just turned the processor off,
so when it powers on again, it won't have any "memory" of what it was doing.

That's ok. We can just keep a flag in RAM and add checks in `main()` to see
what state we were in, right? Well, unfortunately, no.
The bootloader zeroes all of memory as part of its power-on sequence and resets
all global variables to their initial values.
So even though we kept the RAM on during sleep, using up all that power, we don't
get to reap the benefits of keeping it around!
This isn't exactly ideal if you wanted to sleep for 30 seconds before checking
a sensor again and comparing it to the last reading you carefully tucked
away in memory.

Fortunately, Raspberry Pi foresaw this problem and gave us a few special registers
which are retained when we go to sleep and, importantly, are checked _before_ 
the bootloader starts clearing RAM.
These are the `BOOT[0..3]` registers in the powman peripheral, which is always
kept on, even in the lowest sleep states.

These `BOOT` registers are special in that you can store a return address and
stack pointer in them so that when the processor turns back on, the bootloader 
can check them and, if present and correct, jump to the code which it points to.
This skips the whole "RAM re-initialise" bit and retains your global variables.

Because the processor was powered down, we still need to re-initialise the
peripherals. Just be careful about how you do it. You don't want to be
restarting your sensor every time you wake up.

The catch is that the re-entry code to needs to be in RAM.
Typically, pico code is run directly off of flash memory, so we need to do some
work and load it into RAM first.
The effect is that (at least some of) the RAM needs to be kept powered on during
sleep to keep the re-entry code, CPU registers and stack around for wakeup.

This requires some effort, and while it isn't a lot of work, you have to be
careful about it.
Unfortunately, I haven't found any documentation online that says you need to
go through all of this to use the sparkly new power-saver features of the RP2350
to their fullest extent.

Hopefully this explanation can help people understand *why* all of this work is
needed and how to get it set up.

But why is it so complicated for th RP2350? I can sleep my SAMD microcontroller
with a single `wfi` instruction and wake up exactly where I left off, all while
drawing the same amount of power.
While I'm not a microcontroller expert, my best guess is that silicon design is
hard and expensive.

The RP2350 is designed to be a low-cost microcontroller, only at around $1 per
chip.
Compare this to other microcontrollers like the STM32 or SAMD which, for roughly
the same set of features, typically cost more money.

In order to do all of this wakeup reset stuff opaquely (without the programmer
seeing it), there needs to be silicon to do it instead.
I mean, the work still needs to get done, right? And if we aren't doing it,
the hardware has to instead.

The problem is that more hardware features means more silicon, which means more
design time, validation, testing, chances for errors, and simply takes up more
silicon space, all of which increase cost.
To keep the prices as low as they are, Raspberry Pi has opted to not do this in
hardware and make the programmer do it instead.
I feel like that's a fair trade-off really.
A couple hundred bytes of code in exchange for a cheaper microcontroller.

Will this change in the future? Who knows, maybe a third series of RP
microcontrollers will have automatic sleep so we don't need to do all of this,
but that's a long way away, so I won't hold by breath.

## With more Technical Details

Now, this is the part where I admit that I don't know *exactly for sure* what
is happening under the hood, but I think that my guesses are sensible.
If Raspberry Pi wants to confirm this or update their documentation with more
details then I'd love to see it.

So, according to the RP2350 datasheet[^2] the new powman peripheral does, indeed,
power off the switched core inside the RP2350, including peripherals, bus fabric
and more.
This effectively powers off the whole microcontroller, sans the RAM, XIP cache
and always-on (AON) domain.
Inside this AON domain is the powman peripheral, containing the
low-power oscillator (LPOSC), AON timer and power control.
Its this always-on logic that allows the microcontroller to wake itself up.
You can configure up to four wake-up sources from either GPIO inputs or the AON
timer.

When the powman receives a wake up event, it powers things up again.
The core, having just been powered on and retaining no information, starts
execution from the bootloader, and passes control to `boot_stage2`.

`boot_stage2` does lots of things[^4], too many to discuss here, but
one important thing is initialising the C runtime, called `crt0`.
This is common even on desktop PCs and it configures the environment to be able
to run C code.
It involves running constructors, zeroing the BSS section of RAM
and initialising global variables, among other things.

This is really helpful and all, but the problem comes when we already have RAM
set up in a way we want, with nice data that we have carefully arranged and kept
alive while we powered down the core.
When `boot_stage2` is run on a reset, and is not instructed to do otherwise, it
re-initialises the C runtime and overwrites all the RAM we care about (and
ironically, none of the RAM we don't care about).

To avoid this during powman resets, `boot_stage2` checks the four `BOOT` registers
in the powman peripheral[^5] and jumps to that code instead of carrying on with
the typical C runtime initialisation and jump to `main()`[^1].

As mentioned, this code needs to be stored in RAM since the QSPI peripheral
that talks to the flash with all our code on it also got reset during the 
power-down and needs to be reconfigured.

The powman `BOOT` registers store both a reset vector and a stack pointer, so
you can run the code with the same stack frame as when going to sleep.
This means that, if you're careful with the stack, the wake-up code can
restore the return address and all the registers which the sleep function saved
and resume execution like nothing happened.
Make sure that you save _all_ registers, so that the ARM calling convention
isn't inadvertently broken.

All of this requires keeping the RAM powered up, reducing possible power savings.
However, if you were worried about returning to where you left off, you
probably already had some sort of program state that you wanted to retain, so
were unlikely to be in a position to turn off the RAM in the first place.

While we want to avoid the standard initialisation sequence to keep our RAM
intact, we still need to run *some* initialisation functions like properly
resetting the peripherals so we can set them up again properly.
The pico-SDK mentions that the `runtime_init()` function[^7] is useful for
exactly our purposes.

To complicate things, there are two cores on the RP2350 chip, both of which
boot into our reset vector in RAM.
This means that the reset vector also needs to park core1 so that we don't
double-initialise things which usually ends up with either hangs or putting
at least one core into an unknown state.
I haven't tested this with multi-core code, but how hard could that be?
(Probably very hard)

## Credits and Resources

Credits to peterharperuk for publishing a nice [powman example](https://github.com/peterharperuk/pico-examples/commit/7dccd00d15ded4ddf961f44fdcd1f11a9d8c8be1)
which helped me start investigating "properly" the powman.

[^1]: RP2350 datasheet, section 5.2: <https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline>
[^2]: RP2350 datasheet, section 6.3: <https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline>
[^5]: RP2350 datasheet, section 6.4: <https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline>
[^3]: Kevin Boone's explanation of running code in RAM: <https://kevinboone.me/pico_run_ram.html>
[^4]: `boot_stage2` in Pico-SDK GitHub: <https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/boot_stage2/boot2_w25q080.S>
[^7]: pico SDK `runtime_init()` <https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#group_pico_runtime_1gad27ee86dcd85855022a424f61b839d04>

## License

Do whatever you want with this code and information.

