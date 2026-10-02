# MRT Embedded Practice Setup

Before the session, please make sure your development environment is working. You do **not** need to understand all of these tools yet, the goal is just to make sure we can start coding immediately during the session.

## 1. Install VS Code

Download and install Visual Studio Code:

https://code.visualstudio.com/

## 2. Install PlatformIO

Open VS Code and go to **Extensions**.

Search for:

`PlatformIO IDE`

Install it and restart VS Code if prompted.

You should see the PlatformIO alien-head icon on the left side of VS Code.

## 3. Install Git

Download and install Git:

https://git-scm.com/

The default installation options should be fine.

You can verify that Git is installed by opening a terminal and running:

```bash
git --version
```

## 4. Clone this repository

You can either use GitHub Desktop or the terminal.

### GitHub Desktop

Install GitHub Desktop:

https://desktop.github.com/

Then:

1. Select **File → Clone Repository**
2. Select **URL**
3. Paste `https://github.com/McGillRocketTeam/radios-2027.git`
4. Choose where you want to save it

### Terminal

```bash
git clone https://github.com/McGillRocketTeam/radios-2027.git
```

## 5. Open the practice project

In VS Code:

**File → Open Folder**

Open the `radios-2027/radio-bootcamp` folder inside your cloned repository.

Make sure you open the folder containing `platformio.ini`, not just the `src` folder.

You should see something similar to:

```text
platformio.ini
src/
include/
```

## 6. Build the project

PlatformIO may download some tools and libraries the first time you open the project.

Once it finishes, click the **✓ Build** button at the bottom of VS Code.

The build should finish with:

```text
SUCCESS
```

## Before the session

Please make sure:

- [ ] VS Code is installed
- [ ] PlatformIO IDE is installed
- [ ] Git is installed
- [ ] You have cloned this repository
- [ ] You can open the practice project in VS Code
- [ ] The PlatformIO build completes successfully

You do **not** need to upload anything to hardware yet. We will do that together during the session.
