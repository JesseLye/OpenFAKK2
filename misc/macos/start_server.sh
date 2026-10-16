#!/bin/sh
echo "Edit this script to change the path to OpenFAKK2's dedicated server executable."
echo "Set the sv_dlURL setting to a url like http://yoursite.com/openfakk2_path for OpenFAKK2 clients to download extra data."

# sv_dlURL needs to have quotes escaped like \"http://yoursite.com/openfakk2_path\" or it will be set to "http:" in-game.

/Applications/openfakk2/openfakk2ded +set dedicated 2 +set sv_allowDownload 1 +set sv_dlURL \"\" +set com_hunkmegs 64 "$@"
