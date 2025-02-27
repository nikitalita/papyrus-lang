## Table of Contents

1. [About the Project](#about-the-project)
1. [Questions and Help](#questions-and-help)
1. [Getting Started](#getting-started)
1. [Modules and Patterns](#modules-and-patterns)
1. [Useful Resources](#useful-resources)

# About the Project

The VSCode extension is written in TypeScript with C# being used for the language server and debug proxy.

Uses the **[Papyrus Debug Adapter](https://github.com/joelday/papyrus-debug-server) xSE plugin** for live debugging.

Also uses [this fork of **Pyro**](https://github.com/rjstone/pyro) for PPJ builds.

# Questions and Help

Do you have questions or need help? Please come visit the....

[![Discord](https://img.shields.io/discord/558746231665328139.svg?color=%23738ADB) Papyrus Language Tools Discord](https://discord.gg/E4dWujQ)

Even if you don't plan to contribute code, it would be good to hear how you are using the extension and get your feedback.

# Getting Started

## Building

First, you will need Windows with the following installed on your system:

- VSCode [Download and install](https://code.visualstudio.com/)
- Node.js [Download and install](https://nodejs.org/)
- Git for Windows [Download and install](https://git-scm.com/download/win)
- One of the following:
  - Minimum: .NET Core 2.1 SDK [Download and install](https://dotnet.microsoft.com/download/dotnet-core)
  - Recommended: Microsoft Visual Studio 2019 [Download and install](https://visualstudio.microsoft.com/vs/)

Also, building from the shell will be much easier if you use a **Powershell** (powershell.exe) console because the main build script is written in PS, but if you have Visual Studio installed you should be able to build from the solution file.

### 1

First, Fork the `joelday/papyrus-lang` repo on github. Then clone it:

```powershell
git clone https://github.com/<yourusername>/papyrus-lang.git
cd papyrus-lang
```

### 2

Create a branch for your new development:

```powershell
git checkout -b mynewfeature
```

### 3

Set your upstream remote to help you pull changes from main when needed:

```powershell
git remote add upstream https://github.com/joelday/papyrus-lang.git
```

### 4

Make sure your script execution policy allows running unsigned scripts as long as they're already on disk and not executed from the internet:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
Get-ExecutionPolicy -List
```

### 5

Run the build script with default targets:

```powershell
.\build.ps1
```

In the same directory, run this to update the bundled third-party packages (which are provided by separate repos):

```powershell
.\build.ps1 -Script build.cake -Target update-debug-plugin
.\build.ps1 -Script build.cake -Target update-pyro-cli
```

### 6

Run VSCode to open the papyrus-lang directory as a folder. If this is still your current directory then just run:

```powershell
code .
```

Hit **Ctrl-Shift-D** to open the Debug panel. At the top select **Launch (Build Extension Only)**.

Hit **F5** to build and launch the extension with debugging. After a little while you will see another VSCode window open. This is the Extension Development Host version of VSCode running the extension that was just built. Any changes you made to the code would be reflected in the debug/test install of the extension running in this window.

### 7

It is not required that you squash your commits before submitting a pull request, so just commit and push your changes to your fork, then submit the pull request for your branch. Please merge from `upstream/master` to your branch before submitting the pull request though to make sure your changes will merge.

```powershell
git merge upstream/main
```

# Modules and Patterns

This is a brief set of links to more info on modules and patterns used in the code.

- [decoration-ioc](https://github.com/joelday/decoration-ioc) is why `InstantiationService` is used to instantiate services and command handlers based on the Service Locator design pattern. It makes it easy to add references to services in the constructor of a class. For example just put `@IExtensionConfigProvider infoProvider: IExtensionConfigProvider` in the argument list of a constructor and it will magically get called with a reference.
- For a simple example of how to add a command, see `src\papyrus-lang-vscode\src\features\commands\ViewAssemblyCommand.ts`
- [rxjs](https://www.npmjs.com/package/rxjs) is used in many places for the reactive Observer/Observable asynchronous pattern.
- [async/await](https://javascript.info/async-await) is used frequently. If possible try to use async functions and `await` on them because this allows other things to happen while a function is blocked on IO etc.
- [deepmerge](https://www.npmjs.com/package/deepmerge) is used in some places.
- Otherwise most of the code is similar to other vscode extensions.

# Useful Resources

- See the [Project Wiki](https://github.com/joelday/papyrus-lang/wiki)
- [The Typescript Programming Language Documentation](https://www.typescriptlang.org/docs/home.html)
- [VSCode Extension Anatomy](https://code.visualstudio.com/api/get-started/extension-anatomy)
- [VSCode Extension Samples](https://github.com/microsoft/vscode-extension-samples) are useful for learning specific APIs
- [Learn RxJS](https://www.learnrxjs.io/) (Reactive eXtensions for Javascript)
- [JavaScript Promises](https://javascript.info/async)
- The [Pro Git](https://git-scm.com/book/en/v2/) book
- The [Contributing](https://github.com/joelday/papyrus-lang/wiki/Contributing) wiki page
