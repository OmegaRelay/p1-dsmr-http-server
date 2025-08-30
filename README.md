# DSMR P1 HTTP Server
This project provides the firmware for a simple device to expose data from the Dutch-Smart-Meter-Reader protocol P1 over an IP network accessible over HTTP.

This project is intended to be configured in a [West T2 Workspace](https://docs.zephyrproject.org/latest/develop/west/workspaces.html#topologies-supported).


## Getting Started
[Zephyr does not officially support having the topdir as a west workspace](https://docs.zephyrproject.org/latest/develop/west/workspaces.html#not-supported-workspace-topdir-as-git-repository)

There are a few methods to get started but all require installing west as the first step, then do one of the following:

### Clone the git repo
 - Create the workspace directory and `cd` into it.
```bash
mkdir p1-dsmr-http-server && cd p1-dsmr-http-server
```
 - Clone the repo as application.
```bash
git clone https://gitlab.com/OmegaRelay/p1-dsmr-http-server.git application
```
 - Initialise the west workspace.
```bash
west init -l .
```
 - Update dependancies
```bash
west update
```

### Pull the West manifest
 - Create and initialise a west workspace from the application manifest.
```bash
west init -m https://gitlab.com/OmegaRelay/p1-dsmr-http-server p1-dsmr-http-server 
```
 - Update dependancies
```bash
west update
```
