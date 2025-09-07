Build steps
1. Add the tflite-micro library

..  code-block::
west config manifest.project-filter -- +tflite-micro
west config manifest.group-filter -- +optional
west update
```

2. Build 

..  code-block::
west build -p always -b imx8mp_evk/mimx8ml8/adsp .
```
