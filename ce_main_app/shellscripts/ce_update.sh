#!/bin/sh

start=$( date )
echo "ce_update.sh started at : $start"     # show start time

# Stop any cosmosex app, and pass 1st and 2nd argument of this script to the stop script
# When arguments are not given, ce_stop will stop not only CosmosEx app, but also cesuper.sh script, which is fine when called manually from shell by user.
# When 'nosystemctl dontkillcesuper' arguments are given, ce_stop will stop onl CosmosEx app, but not cesuper.sh script, which is what we want when ce_update.sh is called as a part of update process within the cesuper.sh script
/ce/ce_stop.sh $1 $2

echo " "
echo "Updating CosmosEx from internet, this will take a while."
echo "DO NOT POWER OFF THE DEVICE!!!"
echo " "

distro=$( /ce/whichdistro.sh )
url_to_git_repo="https://github.com/atarijookie/ce-atari-releases.git"
url_to_git_api="https://api.github.com/repos/atarijookie/ce-atari-releases/commits/$distro"
url_to_repo_archive="https://github.com/atarijookie/ce-atari-releases/archive/"
url_to_zip_update="$url_to_repo_archive$distro.zip"
path_to_tmp_update="/tmp/$distro.zip"

#---------------------------
# check if should do update from USB

if [ -f /tmp/UPDATE_FROM_USB ]; then                    # if we're doing update from USB
    path_to_usb_update=$( cat /tmp/UPDATE_FROM_USB )    # get content of file into variable
    rm -f /tmp/UPDATE_FROM_USB                          # delete file so we won't do it again next time

    unzip -o $path_to_usb_update -d /ce                 # unzip update into /ce directory, overwrite without prompting
else    # download update from internet, by git or wget
    # check if got git installed
    git --version 2> /dev/null

    if [ "$?" -ne "0" ]; then                           # if don't have git
        echo "Will try to install missing git"

        apt-get update 2> /dev/null
        apt-get --yes --force-yes install git 2> /dev/null # try to install git
    fi

    # try to check for git after possible installation
    git --version 2> /dev/null

    if [ "$?" -ne "0" ]; then                           # git still missing, do it through wget
        echo "git is still missing, will use wget"

        rm -f $path_to_tmp_update                       # delete if file exists
        wget -O $path_to_tmp_update $url_to_zip_update # download to /tmp/yocto.zip

        unzip -o $path_to_tmp_update -d /ce             # unzip update into /ce directory, overwrite without prompting

        # get last online commit in the branch for this distro and store it where we will expect it next time we'll check_for_update (.sh)
        wget -O- $url_to_git_api 2> /dev/null | grep -m 1 'sha' | sed 's/sha//g' | sed 's/[\" \t\n:,]//g' > /ce/update/commit.current

    else                                                # git is present
        echo "doing git pull..."

        cd /ce/                                         # go to /ce directory

        git reset --hard origin/$distro                 # reset all tracked files so git won't complain
        git pull origin $distro                         # try git pull

        if [ "$?" -ne "0" ]; then                       # git complained that this is not a repo? as it is not empty, simple 'git clone' might fail
            echo "doing git fetch..."

            cd /ce/
            git init                                    # make this dir a repo
            git remote add origin $url_to_git_repo      # set origin to url to repo
            git fetch --depth=1                         # fetch, but only 1 commit deep
            git checkout -f $distro                     # switch to the right repo
            git pull origin $distro                     # just to be sure :)
        fi
    fi
fi

#--------------------------
# add execute permissions to scripts and binaries (if they don't have them yet)
chmod +x /ce/app/cosmosex
chmod +x /ce/update/flash_stm32
chmod +x /ce/update/flash_xilinx
chmod +x /ce/*.sh
chmod +x /ce/update/*.sh

#--------------------------
# check what chips we really need to flash, possibly force flash, and do the flashing

/ce/update/check_and_flash_chips.sh

#--------------

# on jessie update SysV init script, remove systemctl service
if [ "$distro" = "jessie" ]; then
    cp "/ce/initd_cosmosex" "/etc/init.d/cosmosex"
    rm -f "/etc/systemd/system/cosmosex.service"
fi

# on stretch remove SysV init script, update systemctl service
if [ "$distro" = "stretch" ]; then
    rm -f "/etc/init.d/cosmosex"
    cp "/ce/cosmosex.service" "/etc/systemd/system/cosmosex.service"
fi

#------------------------
# now add / change the core_freq param in the boot config to avoid SPI clock issues
coreFreqCountAny=$( cat /boot/config.txt | grep core_freq | wc -l )
coreFreqCountCorrect=$( cat /boot/config.txt | grep 'core_freq=250' | wc -l )

addCoreFreq=0           # don't add it yet

# A) no core_freq? Add it
if [ "$coreFreqCountAny" -eq "0" ]; then
    echo "No core_freq in /boot/config.txt, adding it"
    addCoreFreq=1
fi

# B) more than one core_freq? Remove them, then add one
if [ "$coreFreqCountAny" -gt "1" ]; then
    echo "Too many core_freq in /boot/config.txt, fixing it"
    addCoreFreq=1
fi

# C) There is some core_freq, but it's not correct? Remove it, then add correct one
if [ "$coreFreqCountAny" -gt "0" ] && [ "$coreFreqCountCorrect" -eq "0" ]; then
    echo "core_freq in /boot/config.txt is incorrect, fixing it"
    addCoreFreq=1
fi

# if we need to add the core_freq
if [ "$addCoreFreq" -gt "0" ]; then
    mv /boot/config.txt /boot/config.old                            # back up old
    cat /boot/config.old | grep -v 'core_freq' > /boot/config.txt   # create new without any core_freq directive

    # now append the correct core_config on the end of file
    echo "core_freq=250" >> /boot/config.txt
else
    # we don't need to do anything for case D), where there is one core_freq there and it's correct
    echo "core_freq is ok in /boot/config.txt"
fi

#------------------------
# disable ctrl-alt-del causing restart
ln -fs /dev/null /lib/systemd/system/ctrl-alt-del.target

# disable auto-login
ln -fs /lib/systemd/system/getty@.service /etc/systemd/system/getty.target.wants/getty@tty1.service

sync

#------------------------
# if should reboot after this (e.g. due to network settings reset), do it now
if [ -f /tmp/REBOOT_AFTER_UPDATE ]; then
    rm -f /tmp/REBOOT_AFTER_UPDATE          # delete file so we won't do it again next time
    reboot now
fi

echo " "
echo "Update done, you may start the /ce/ce_start.sh now!";
echo " "

stop=$( date )
echo "ce_update.sh finished at: $stop"     # show stop time
