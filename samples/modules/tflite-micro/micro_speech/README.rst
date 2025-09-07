.. zephyr:code-sample:: tflite-microspeech-openamp
   :name: Microspeech Openamp

Overview
********************

Building and Running
********************

Add the tflite-micro module to your West manifest and pull it:

.. code-block:: console

   west config manifest.project-filter -- +tflite-micro
   west config manifest.group-filter -- +optional
   west update

Build the project

.. code-block:: console

   west build -p always -b imx8mp_evk/mimx8ml8/adsp .

Sample Output
*************

Known limitation
****************
