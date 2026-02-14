---
date: "2026-01-08T17:48:22+05:30"
title: 'Soan Papdi FPGA - Setup Guide'
thumbnail: "/soanpapdi/sp.jpeg"

url: /posts/soanpapdi-setup
alias:
  - /soanpapdi
  - /sp
tags:
  - "soanpapdi"

categories:
  - "setup"

series:
  - "basics"
---

![](/soanpapdi/sp.jpeg "A Quick Guide to getting up and running with the Soan Papdi FPGA board.")

A quick guide to getting up and running with the Soan Papsi FPGA board.

<!--more-->

# Unboxing and Board Walkthrough

{{< youtube U5uXXChxLwI >}}

---

# Soan Papdi - Software Setup

{{< youtube mDM6PSjPgIU >}}

The official version of iCEstudio doesn't have the support for Soan Papdi just yet. We thus have to build everything from source until the changes have landed in the official release.

As a summary, we are to do the following:
1. Patch the `iCEStudio` repo.
2. Patch the `apio` python package which is responsible for building the RTL and uploading to the board.

**Assumption**:
- You have `nodejs` installed.
- You can navigate nonde issues on your own by google searching.

## Setting up iCEStudio
1. Clone the repo
    ```bash
    git clone https://github.com/streetdogg/icestudio.git
    ```
2. This should get you the source code. We are to work on the soanpapdi branch of this repo. I have already added the changes there.
    ```bash
    git switch soanpapdi
    ```
3. You need to have node installed.
4. Assuming node is available on your system. Execute the following commands from within the repository’s root.
    ```bash
    npm install
    npm run prepare
    npm start
    ```
    This should install the dependencies and also start `iCEStudio`
5. It may ask to select a board, pick `Soan-Papdi`.
6. If asked about toolchain installation, click the popup to fetch and install the toolchain. In this case - `apio`.
7. If asked about installing drivers, choose to install it. Click on the popup.
8. Open an example: `File–> Examples–> Basic–> 6. ...`. This a simple switch to **LED** connection.
9. If it complains about board. Override and proceed anyways.
10. Select the board again, `Select —> Board —> Soan-Papdi`.
11. For the **Pushbutton**, select **A0** from the drop down. And, **D0** for the **LED**.
12. From the status bar in the bottom. On the left, the **gear icon** is to compile and download icon is to upload the bit stream to the board.
13. Press the **gear** icon to compile. It should complain - `Unknown Board`.

## Patching APIO
The toolchain is unaware of the board we are using - `Soan-Papdi`. We now need to find the location of this python package and update the boards.json with programming information about our board.
1. After the error in #13 about, check the command output: `View —> Command Ouput`.
2. This should open a console window with the output messages resulted as a result of `#13`. Should look something like so -
    ```bash
    …
    Fusion.app/Contents/Public:/Applications/iTerm.app/Contents/Resources/utilities"
    "/Users/piyush/.icestudio/venv/bin/apio" build --board Soan-Papdi --top-module
    main -p
    "/Users/piyush/workspace/soanpapdi/icestudio/app/resources/collection/examples/1.
    Basic/ice-build/06. Pushbutton and LED"

    Info: No apio.ini file
    Error: unknown board: Soan-Papdi
    ```
3. The toolchain is install in Python virtual environment, from the logs above, the following provides the hint to the path - `/Users/piyush/.icestudio/venv/`
4. The `boards.json` happens to be saved at -
`/Users/piyush/.icestudio/venv/lib/python3.13/site-packages/apio/resources/boards.json`. The location of this file might vary slightly based on the python version you have installed.
5. Add the following to the file:
    ```json
    "Soan-Papdi": {
        "name": "Soan-Papdi",
        "fpga": "iCE40-UP5K-SG48",
        "programmer": {
            "type": "dfu"
        },
        "usb": {
            "vid": "1d50",
            "pid": "6146"
        },
        "ftdi": {
            "desc": "Soan-Papdi.*"
        }
    },
    ```
6. Save and close the file. `apio` should not complain.

## Generating and uploading the bitstream
1. We are all set with iCEStudio and apio.
2. Attempt generating the bitstream by pressing the gear icon in the status bar (#13 from above).
3. This should be a success.

## Uploading Bitstream to the board
1. Prepare the board by changing it to programming mode.
    1. Press the `PROG` button. Hold it.
    2. Press and release `RESET` button.
    3. White `LED` should glow.
    4. Realse the `PROG` button after the white `LED` is turned off.
    5. The board is in programming mode and ready to access the bitstream.
2. Upload the bitstream
    1. Click the **Download** icon in the status bar.
    2. Wait for the upload to complete.
3. Once the bitstream is uploaded, Press and Release the `RESET` button to change the FPGA mode. The uploaded bitstream should be active.

**end**.
