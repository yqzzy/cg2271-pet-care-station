# Run the MCXC444 pet station

1. Clone or pull the whole repository. Open MCUXpresso IDE 25.6.136 and choose a workspace outside the clone. The project uses the FRDM-MCXC444 SDK 26.06.00 and includes the board drivers, startup files and FreeRTOS sources.
2. Choose **File > Import > General > Existing Projects into Workspace**. For **Select root directory**, browse to **mcuxpresso/pet-station** in the clone. Select **pet-station**, leave **Copy projects into workspace** unchecked, and click **Finish**. If the workspace already has a project with that name, switch to a fresh workspace first.
3. Select **pet-station** and click **Build** (hammer icon). The default Debug build produces `Debug/pet-station.axf`. The project's **mcxc444** folder links to the clone's **firmware/mcxc444**, so source edits and pulls feed directly into the next build.
4. Connect the FRDM-MCXC444 debug-probe USB port using a data cable. With **pet-station** selected, choose **Debug** in the Quickstart panel, select the board's LinkServer probe when prompted, and click **Resume (F8)** if execution stops at main.

The ESP32 uses its own firmware and is programmed separately.

## Windows command-line build

From the repository root, run `.\mcuxpresso\build.ps1`. Pass `-Configuration Release` for Release or `-Ide 'D:\path\to\ide\mcuxpressoidec.exe'` for a different IDE installation. The script builds the MCXC444 firmware using a separate workspace under the ignored build directory.
