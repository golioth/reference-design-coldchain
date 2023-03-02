Golioth Cold Chain Tracker Reference
####################################

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

Local set up
************

Do not clone this repo using git. Zephyr's ``west`` meta tool should be used to
set up your local workspace.

Install the Python virtual environment (recommended)
====================================================

.. code-block:: console

   cd ~
   mkdir golioth-reference-design-gps
   python -m venv golioth-reference-design-gps/.venv
   source golioth-reference-design-gps/.venv/bin/activate
   pip install wheel west

Use ``west`` to initialize and install
======================================

.. code-block:: console

   cd ~/golioth-reference-design-gps
   west init -m git@github.com:golioth/reference-design-gps.git .
   west update
   west zephyr-export
   pip install -r deps/zephyr/scripts/requirements.txt

This will also install the `golioth-zephyr-boards`_ definitions for the Golioth
Aludel-Mini.

Building the application
************************

Build Zephyr sample application for Golioth Aludel-Mini
(``aludel_mini_v1_sparkfun9160_ns``) from the top level of your project. After a
successful build you will see a new ``build`` directory. Note that any changes
(and git commmits) to the project itself will be inside the ``app`` folder. The
``build`` and ``deps`` directories being one level higher prevents the repo from
cataloging all of the changes to the dependencies and the build (so no
``.gitignore`` is needed)

During building, replace ``<your.semantic.version>`` to utilize the DFU
functionality on this Reference Design.

.. code-block:: console

   $ (.venv) west build -b aludel_mini_v1_sparkfun9160_ns app -- -DCONFIG_MCUBOOT_IMAGE_VERSION=\"<your.semantic.version>\"
   $ (.venv) west flash

Configure PSK-ID and PSK using the device shell based on your Golioth
credentials and reboot:

.. code-block:: console

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:~$ kernel reboot cold

Golioth Features
****************

This app implements:

* Over-the-Air (OTA) firmware updates * LightDB Stream for recording periodic
  GPS and weather sensor readings to the `gps` endpoint.
* Settings Service to adjust the delay between recording GPS readings, and the
  delay between sending cached readings to Golioth
* Remote Logging
* Remote Procedure call (RPC) to reboot the device

This reference design uses the following Settings Service keys. Add these
key/value pairs in the Device Settings menu of the `Golioth Console`_.

* ``LOOP_DELAY_S`` (seconds between uploading cached data to Golioth)
* ``GPS_DELAY_S`` (settings to wait between recording GPS data)

.. _Golioth Console: https://console.golioth.io
.. _golioth-zephyr-boards: https://github.com/golioth/golioth-zephyr-boards
