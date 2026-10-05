# VB-CABLE

A virtual audio cable for Windows by [VB-Audio Software](https://vb-audio.com/Cable/),
signed by Microsoft: it installs on any Windows 10 or 11 without test-signing
mode. RTR-Bench uses it, for now, to receive the sound of any player.

It is **donationware of VB-Audio, not part of RTR-Bench and not open
source**. Its installer is not kept in this repository: `scripts\get-vbcable.ps1`
fetches it from the VB-Audio site into this folder.

## Install

1. `scripts\get-vbcable.ps1`
2. In `extras\vb-cable\Pack45\`, right click `VBCABLE_Setup_x64.exe` > Run as
   administrator > Install Driver.
3. Restart Windows.

## Use with the bench

1. In the player, or per program in the volume mixer of Windows, choose
   **CABLE Input (VB-Audio Virtual Cable)** as the output device. Nothing
   sounds on the speakers.
2. Start RTR-Bench (the rack lists the audio devices when it starts). The
   audio strip has an **IN** group named *CABLE Output*: take a cable at its
   L or R jack and plug it into a point of the schematic.
3. Wire the point where the processed signal is to an **OUT** group, the
   speakers.

The project's own cable, [RTR-Cable](https://github.com/HermesSilva/RTR-Cable),
is open source but test-signed: it needs Windows in test-signing mode.
