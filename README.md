# OpenGL Template

Just a simple OpenGL template that I use on my streams.

## Controls

| Shortcut                 | Description                                                                                                                                            |
|--------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------|
| <kbd>q</kbd>             | Quit                                                                                                                                                   |
| <kbd>F5</kbd>            | Reload [render.conf](./render.conf) and all the resources refered by it. Red screen indicates an error, check the output of the program if you see it. |
| <kbd>F6</kbd>            | Make a screenshot.                                                                                                                                     |
| <kbd>SPACE</kbd>         | Pause/unpause the time uniform variable in shaders                                                                                                     |
| <kbd>←</kbd><kbd>→</kbd> | In pause mode step back/forth in time.                                                                                                                 |

## Shader Uniforms

| Name         | Type        | Description                                                                          |
|--------------|-------------|--------------------------------------------------------------------------------------|
| `resolution` | `vec2`      | Current resolution of the screen in pixels                                           |
| `time`       | `float`     | Amount of time passed since the beginning of the application when it was not paused. |
| `mouse`      | `vec2`      | Position of the mouse on the screen in pixels                                        |
| `tex`        | `sampler2D` | Current texture                                                                      |
