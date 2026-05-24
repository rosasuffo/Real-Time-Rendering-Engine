He implementado lo siguiente:
- Material PBR en la pasada de Deffered.
- Pasada de profundidad.
- Screen Space Ambient Occlusion.
- He empezado a implementar el cálculo de Shadow Mapping, pero solo he completado la pasada del shadow map, no me ha dado tiempo a meterlo en el pase de composición final.

Para ello, he modificado/añadido los siguientes archivos:
- Modificado engine.cpp para adaptar las render passes.
- Modificado archivos de defferedPassVK para añadir el material PBR.
- Añadido los archivos de depthPassVK para la prepasada de profundidad.
- Añadido los archivos de SSAOPass para la pasada de visibilidad del SSAO.
- Añadido los archivos de SSAOBlurPass para blurrear la pasada del SSAO.
- Añadido los archivos de shadowPass para calcular la profundidad desde el punto de vista de una luz.
- Modificado el pase de composición para juntar toda la información.
- Los shaders que se incluyen en la entrega también han sido todos modificados para lograr los efectos.
- Se ha añadido una escene cbox.xml para probar mejor el SSAO.

Rosa Suffo García.