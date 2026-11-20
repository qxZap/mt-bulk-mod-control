# Mod Switch

Enable All and Disable All on Motor Town's **Mods [Review]** screen.

The screen (native `MTLoadedModListWidget`, one `MTLoadedModRowWidget` per pak) only switches mods one at a time.
Mod Switch adds two buttons beside Keep Disabled / Restore Previous Set. A click presses each row's own button whose
next-launch state is the other one, the way a player would, so the game keeps its own bookkeeping in
`Saved\Config\Windows\CustomPak.ini` `[CustomPakStateV2]`. As with the game's own buttons, the change applies on the
next launch.

`plugins\mod_switch.ini`: `[General] Disabled=1` turns the plugin off.

## Install

Needs [Plugin Loader](https://github.com/qxZap/mt-plugin-loader) (`version.dll`) in `MotorTown\Binaries\Win64\`.
Put `mt_plugin_ModSwitch.dll` in `MotorTown\Binaries\Win64\plugins\`.

## Build

`build.bat` (MSVC 2022, x64) writes `out\mt_plugin_ModSwitch.dll`.
