// NestBox for the menu bar: supervises the companion helper, runs the developer pet window on request, and shows
// what the pet is doing and what is sent to it.
//
//   NestBox.app/Contents/MacOS/NestBox [--home DIR] [--device URL] [--name NAME] [--sim-args "ARGS"] [--serial PATH]
//   NestBox --render-icon out.png
//   NestBox --app-icon DIR.iconset
AppLauncher.run(arguments: Array(CommandLine.arguments.dropFirst()))
