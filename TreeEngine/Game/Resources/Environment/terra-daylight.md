# Terra daylight environment

These cubemaps were generated for the sun direction and 2-degree angular diameter in `Scenes/terra.scene.json` using Blender 5.2's multiple-scattering sky. The background includes a visible sun disc; the diffuse and specular lighting maps omit it because the scene's directional light supplies the direct sun. The lighting uses a 0.045 radiance scale and an approximate ground bounce. The background extends the horizon color below the horizon to avoid introducing a second landscape.

To regenerate, use Blender's bundled Python and run from the repository root (replace `<scratch>` with a writable directory):

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' -b --disable-autoexec --python Misc\render_terra_daylight.py -- <scratch>
& 'C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe' Misc\bake_terra_daylight.py <scratch>
Copy-Item <scratch>\terra-daylight-*.dds TreeEngine\Game\Resources\Environment\
```

The generator's `--sun` is the direction **toward** the sun, opposite the scene's directional light vector. Pass `--sun X Y Z` and `--diameter DEGREES` to match any changes to that light; the sky and lighting are static until regenerated. `--scale` adjusts the environment radiance.

## Sun size and direction

The 2-degree angular diameter is the apparent width of the sun in the sky. It controls the visible disc and the range of light directions sampled for soft shadows: a larger diameter gives broader, softer shadow edges. Terra's 2-degree sun is an artistic choice; the real sun appears about half a degree wide.

The scene specifies the direction light travels as `light.direction: [0.625929, -0.714180, 0.313303]`. The components are X, Y and Z in TreeEngine coordinates, with Y pointing up; the negative Y component points the light down toward the terrain. The direction from the scene toward the sun is the opposite vector, `[-0.625929, 0.714180, -0.313303]`. This is the direction accepted by the generator's `--sun` option.
