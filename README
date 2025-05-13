### Building
In order to build the plugins for a specific version of Okular navigate to `release/<desired version>/` and execute the script `./build.sh`.

In order to build the plugins for all supported versions of Okular, navigate to `/releases/` and execute `./build-all.sh`.

### Testing
After you've built the plugin, you can begin testing. Testing is mostly automated, except for the final step of testing interaction, which should be very brief if everything works as expected.

Testing is done inside of distroboxes which allow for testing on many different linux distributions.

In order to test the plugin for one specific distribution, navigate to `testing/<desired linux distro>/` and run `./test.sh`. If you want to run a clean test on a brand new distrobox then add then use the option `--clean` to first delete the old distrobox and then make a new one.

If you want to instead test all supported operating systems then navigate to `testing/` and execute `./test-all.sh` again including the optional `--clean` argument.

You will need to give root permissions to the script as it executes to allow it to install the plugin into the distrobox, and to copy the built plugin into the home folder of the distrobox.
