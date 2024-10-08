..
   Copyright (c) 2022-2024 Golioth, Inc.
   SPDX-License-Identifier: Apache-2.0

Golioth Cold Chain Tracker Reference Design
###########################################

Overview
********

The Golioth Cold Chain Tracker monitors temperature and records readings along
with GPS location/time data. This is a common use-case for shipping
temperature-sensitive goods, providing proof that the cold chain was maintained
during transport.

GPS readings can be received as frequently as once-per-second. Each packet of
GPS data is combined with temperature/pressure/humidity data and uploaded to
Golioth as a historical record using the GPS timestamp. The reference design
caches data, so that when the device is out of cellular range, it can still
record for later upload.

Supported Hardware
******************

This firmware can be built for a variety of supported hardware platforms.

.. pull-quote::
   [!IMPORTANT]

   In Zephyr, each of these different hardware variants is given a unique
   "board" identifier, which is used by the build system to generate firmware
   for that variant.

   When building firmware using the instructions below, make sure to use the
   correct Zephyr board identifier that corresponds to your follow-along
   hardware platform.

.. list-table:: **Follow-Along Hardware**
   :header-rows: 1

   * - Hardware
     - Zephyr Board
     - Follow-Along Guide

   * - .. image:: images/golioth-coldchain_tracker-fah-nrf9160dk.jpg
          :width: 240
     - ``nrf9160dk_nrf9160_ns``
     - `nRF9160 DK Follow-Along Guide`_

.. list-table:: **Custom Golioth Hardware**
   :header-rows: 1

   * - Hardware
     - Zephyr Board
     - Project Page
   * - .. image:: images/golioth-coldchain_tracker-aludel_mini_v1_photo_top.jpg
          :width: 240
     - ``aludel_mini_v1_sparkfun9160_ns``
     - `Cold Chain Asset Tracker Project Page`_

Local set up
************

.. pull-quote::
   [!IMPORTANT]

   Do not clone this repo using git. Zephyr's ``west`` meta tool should be used to
   set up your local workspace.

Install the Python virtual environment (recommended)
====================================================

.. code-block:: shell

   cd ~
   mkdir golioth-reference-design-coldchain
   python -m venv golioth-reference-design-coldchain/.venv
   source golioth-reference-design-coldchain/.venv/bin/activate
   pip install wheel west

Use ``west`` to initialize and install
======================================

.. code-block:: shell

   cd ~/golioth-reference-design-coldchain
   west init -m https://github.com/golioth/reference-design-coldchain.git .
   west update
   west zephyr-export
   pip install -r deps/zephyr/scripts/requirements.txt

Building the application
************************

Build the Zephyr sample application for the `Nordic nRF9160 DK`_
(``nrf9160dk_nrf9160_ns``) from the top level of your project. After a
successful build you will see a new ``build`` directory. Note that any changes
(and git commits) to the project itself will be inside the ``app`` folder. The
``build`` and ``deps`` directories being one level higher prevents the repo from
cataloging all of the changes to the dependencies and the build (so no
``.gitignore`` is needed).

Prior to building, update ``VERSION`` file to reflect the firmware version number you want to assign
to this build. Then run the following commands to build and program the firmware onto the device.


.. pull-quote::
   [!IMPORTANT]

   You must perform a pristine build (use ``-p`` or remove the ``build`` directory)
   after changing the firmware version number in the ``VERSION`` file for the change to take effect.

.. code-block:: text

   $ (.venv) west build -p -b nrf9160dk/nrf9160/ns --sysbuild app
   $ (.venv) west flash

Configure PSK-ID and PSK using the device shell based on your Golioth
credentials and reboot:

.. code-block:: text

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:~$ kernel reboot cold

Add Pipeline to Golioth
***********************

Golioth uses `Pipelines`_ to route stream data. This gives you flexibility to change your data
routing without requiring updated device firmware.

Whenever sending stream data, you must enable a pipeline in your Golioth project to configure how
that data is handled. Add the contents of ``pipelines/batch-json-to-lightdb-stream.yml`` as a new
pipeline as follows:

   1. Navigate to your project on the Golioth web console.
   2. Select ``Pipelines`` from the left sidebar and click the ``Create`` button.
   3. Give your new pipeline a name and paste the pipeline configuration into the editor.
   4. Click the toggle in the bottom right to enable the pipeline and then click ``Create``.

All data streamed to Golioth in JSON format will now be routed to LightDB Stream and may be viewed
using the web console. You may change this behavior at any time without updating firmware simply by
editing this pipeline entry.

Golioth Features
****************

This app implements:

* Over-the-Air (OTA) firmware updates
* LightDB Stream for recording periodic GPS and weather sensor readings to the
  ``gps`` endpoint.
* Settings Service to adjust the delay between recording GPS readings, and the
  delay between sending cached readings to Golioth
* Remote Logging
* Remote Procedure call (RPC) to reboot the device

Settings Service
================

The following settings should be set in the Device Settings menu of the
`Golioth Console`_.

``LOOP_DELAY_S`` Adjusts the delay between sending reading to Golioth. Each time this delay passes,
   cached GPS/Temperature readings will be uploaded (along with battery info if applicable). Set to
   an integer value (seconds).

   Default value is ``5`` seconds.

``GPS_DELAY_S``
   Adjusts the delay between caching GPS readings. Set to an integer value (seconds).

   Default value is ``3`` seconds.

Remote Procedure Call (RPC) Service
===================================

The following RPCs can be initiated in the Remote Procedure Call menu of the
`Golioth Console`_.

``get_network_info``
   Query and return network information.

``reboot``
   Reboot the system.

``set_log_level``
   Set the log level.

   The method takes a single parameter which can be one of the following integer
   values:

   * ``0``: ``LOG_LEVEL_NONE``
   * ``1``: ``LOG_LEVEL_ERR``
   * ``2``: ``LOG_LEVEL_WRN``
   * ``3``: ``LOG_LEVEL_INF``
   * ``4``: ``LOG_LEVEL_DBG``


Hardware Variations
*******************

This reference design may be built for a variety of different boards.

This reference design may be build for the `Nordic nRF9160 DK`_, with the
`MikroE Arduino UNO click shield`_ to interface the two click boards.

* Position the WEATHER click in Slot 1
* Position the GNSS 7 click in Slot 2

The click boards must be in this order for the GPS UART to work.

Prior to building, update ``VERSION`` file to reflect the firmware version number you want to assign
to this build. Then run the following commands to build and program the firmware onto the device.

.. code-block:: console

   $ (.venv) west build -p -b aludel_mini/nrf9160/ns --sysbuild app
   $ (.venv) west flash

Golioth Aludel Elixir
=====================

This reference design may be built for the Golioth Aludel Elixir board. By default this will build
for the latest hardware revision of this board.

.. code-block:: text

   $ (.venv) west build -p -b aludel_elixir/nrf9160/ns --sysbuild app
   $ (.venv) west flash

To build for a specific board revision (e.g. Rev A) add the revision suffix ``@<rev>``.

.. code-block:: text

   $ (.venv) west build -p -b aludel_elixir@A/nrf9160/ns --sysbuild app
   $ (.venv) west flash

OTA Firmware Update
*******************

This application includes the ability to perform Over-the-Air (OTA) firmware updates:

1. Update the version number in the `VERSION` file and perform a pristine (important) build to
   incorporate the version change.
2. Upload the `build/app/zephyr/zephyr.signed.bin` file as an artifact for your Golioth project
   using `main` as the package name.
3. Create and roll out a release based on this artifact.

Visit `the Golioth Docs OTA Firmware Upgrade page`_ for more info.

External Libraries
******************

The following code libraries are installed by default. If you are not using the
custom hardware to which they apply, you can safely remove these repositories
from ``west.yml`` and remove the includes/function calls from the C code.

* `golioth-zephyr-boards`_ includes the board definitions for the Golioth
  Aludel-Mini
* `libostentus`_ is a helper library for controlling the Ostentus ePaper
  faceplate
* `zephyr-network-info`_ is a helper library for querying, formatting, and returning network
  connection information via Zephyr log or Golioth RPC

Using this template to start a new project
******************************************

Fork this template to create your own Reference Design. After checking out your fork, we recommend
the following workflow to pull in future changes:

* Setup

  * Create a ``template`` remote based on the Reference Design Template repository

* Merge in template changes

  * Fetch template changes and tags
  * Merge template release tag into your ``main`` (or other branch)
  * Resolve merge conflicts (if any) and commit to your repository

.. code-block:: shell

   # Setup
   git remote add template https://github.com/golioth/reference-design-template.git
   git fetch template --tags

   # Merge in template changes
   git fetch template --tags
   git checkout your_local_branch
   git merge template_v1.0.0

   # Resolve merge conflicts if necessary
   git add resolved_files
   git commit

.. _nRF9160 DK Follow-Along Guide: https://projects.golioth.io/reference-designs/cold-chain-tracker/guide-nrf9160-dk
.. _Cold Chain Asset Tracker Project Page: https://projects.golioth.io/reference-designs/cold-chain-tracker
.. _Golioth Console: https://console.golioth.io
.. _Nordic nRF9160 DK: https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk
.. _Pipelines: https://docs.golioth.io/data-routing
.. _the Golioth Docs OTA Firmware Upgrade page: https://docs.golioth.io/firmware/golioth-firmware-sdk/firmware-upgrade/firmware-upgrade
.. _MikroE Arduino UNO click shield: https://www.mikroe.com/arduino-uno-click-shield
.. _golioth-zephyr-boards: https://github.com/golioth/golioth-zephyr-boards
.. _libostentus: https://github.com/golioth/libostentus
.. _zephyr-network-info: https://github.com/golioth/zephyr-network-info
