This is the official source code for the ScreenShareOTR plugin created by Yarik "Dyka" Wood as seen on https://ss-otr.jabberplugins.net

In its current state, this plugin has been fully tested on Windows XP - 11 & Linux (Fedora, Debian, Ubuntu)

The plugin uses a reverse-tunneling SocketIO-server (to bypass NAT) on https://jabberplugins.net (hosted by me) 
which is used for routing OTR-encrypted (if enabled) screenshare packets between you & your buddy. After clicking
"Share screen" from within your conversation with any given buddy, you can then select which window you wish to
share OR you can also share the entire desktop. Note that closing the viewer window or clicking "stop" from the
screensharer window will close/terminate the screenshare tunnel & your desktop/window will no longer be streamed.

If you find any bugs or want to make some contributions to the plugin feel free to open a pull request
