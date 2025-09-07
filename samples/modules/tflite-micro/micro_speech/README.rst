.. zephyr:code-sample:: tflite-micro-speech-open-amp
   :name: Microspeech Openamp

Build steps
1. Add the tflite-micro library

.. code-block:: console
west config manifest.project-filter -- +tflite-micro
west config manifest.group-filter -- +optional
west update
```

2. Build 

.. code-block:: console
west build -p always -b imx8mp_evk/mimx8ml8/adsp .
```
