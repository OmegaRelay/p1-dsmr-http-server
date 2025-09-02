<!-- PROJECT SHIELDS -->
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![License][license-shield]][license-url]

<!-- PROJECT LOGO -->
<br />
<div align="center">

  <pre align="center">
DSMR P1 HTTP Server
  </pre>

  <p align="center">
    The Zephyr based open firmware for exposing the Dutch-Smart-Meter-Readings read via the P1 port over HTTP
    <br />
    <br />
    <a href="https://gitlab.com/OmegaRelay/p1-dsmr-http-server/-/issues/new?issuable_template=bug-report">Report Bug</a>
    &middot;
    <a href="https://gitlab.com/OmegaRelay/p1-dsmr-http-server/-/issues/new?description_template=feature-request">Request Feature</a>
  </p>
</div>


## Getting Started
[Zephyr does not officially support having the topdir as a west workspace](https://docs.zephyrproject.org/latest/develop/west/workspaces.html#not-supported-workspace-topdir-as-git-repository) as such this project is intended to be configured in a [West T2 Workspace](https://docs.zephyrproject.org/latest/develop/west/workspaces.html#topologies-supported) topology.


There are a few methods to get started but all require installing west as the first step, then do one of the following:

### Clone the git repo
 - Create the workspace directory and `cd` into it.
```bash
mkdir west-workspace && cd west-workspace
```
 - Clone the repo as application and `cd` into it.
```bash
git clone https://gitlab.com/OmegaRelay/p1-dsmr-http-server.git
```
 - Initialise the west workspace.
```bash
west init -l p1-dsmr-http-server
```
 - Update dependancies
```bash
west update
```

### Pull the West manifest
 - Create and initialise a west workspace from the application manifest. This will create the workspace and pull the application into it.
```bash
west init -m https://gitlab.com/OmegaRelay/p1-dsmr-http-server west-workspace
```
 - `cd` into the application directory.
```bash
cd west-workspace/p1-dsmr-http-server
```
 - Update dependancies.
```bash
west update
```

<!-- MARKDOWN LINKS & IMAGES -->
<!-- https://www.markdownguide.org/basic-syntax/#reference-style-links -->
[contributors-shield]: https://img.shields.io/gitlab/contributors/OmegaRelay/p1-dsmr-http-server.svg?style=for-the-badge
[contributors-url]: https://gitlab.com/OmegaRelay/p1-dsmr-http-server/-/graphs/main
[forks-shield]: https://img.shields.io/gitlab/forks/OmegaRelay/p1-dsmr-http-server.svg?style=for-the-badge
[forks-url]: https://gitlab.com/OmegaRelay/p1-dsmr-http-server/-/forks
[stars-shield]: https://img.shields.io/gitlab/stars/OmegaRelay/p1-dsmr-http-server.svg?style=for-the-badge
[stars-url]: https://gitlab.com/OmegaRelay/p1-dsmr-http-server/-/starrers
[issues-shield]: https://img.shields.io/gitlab/issues/open/OmegaRelay/p1-dsmr-http-server.svg?style=for-the-badge
[issues-url]: https://gitlab.com/OmegaRelay/p1-dsmr-http-server/-/issues
[license-shield]: https://img.shields.io/gitlab/license/OmegaRelay/p1-dsmr-http-server.svg?style=for-the-badge
[license-url]: https://gitlab.com/OmegaRelay/p1-dsmr-http-server/blob/main/LICENSE
