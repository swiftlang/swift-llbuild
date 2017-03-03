# -*- mode: ruby -*-
# vi: set ft=ruby :

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure(2) do |config|
  # We use Ubunutu 16.04 as our standard development base.
  config.vm.box = "bento/ubuntu-16.04"

  # Sync the sources.
  #config.vm.synced_folder ".", "/src/llbuild"

  # Support parallel builds.
  config.vm.provider "vmware_fusion" do |v|
    v.vmx["memsize"] = "2048"
    v.vmx["numvcpus"] = "4"
  end
  config.vm.provider "virtualbox" do |v|
    v.memory = 2048
    v.cpus = 4
  end

  # Disable syncing by default.
  config.vm.synced_folder ".", "/vagrant", disabled: true
  
  # Provision build tools, source dependencies, and testing tools.
  #
  # Some llbuild developers are also known to be emacs users.
  config.vm.provision "shell", inline: <<-SHELL
    sudo apt-get update
    sudo apt-get install -y clang cmake ninja-build
    sudo apt-get install -y libncurses-dev libsqlite3-dev
    sudo apt-get install -y python-pip
    sudo pip install lit
    sudo apt-get install -y emacs
  SHELL
end
