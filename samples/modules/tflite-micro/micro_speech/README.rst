.. zephyr:code-sample:: tflite-micro-speech-openamp
   :name: Microspeech Openamp

Building and Running
********************

Add the tflite-micro module to your West manifest and pull it:

.. code-block:: console

west config manifest.project-filter -- +tflite-micro
west config manifest.group-filter -- +optional
west update


Build

.. code-block:: console

west build -p always -b imx8mp_evk/mimx8ml8/adsp .

Sample Output
*************

Known limitation
****************
