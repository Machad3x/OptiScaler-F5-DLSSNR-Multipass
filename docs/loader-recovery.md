# D3D12 setup when a load could not be handled

OptiScaler normally sets up its D3D12 hooks while the game loads `d3d12.dll`. Three cases stop that from happening at that moment:

- the load runs on a thread without a TLS array (World of Warcraft's loader does this), where OptiScaler's load handling cannot run;
- OptiScaler's own load of the file fails, but the game's load works (an older `d3d12.dll` next to the exe can do this);
- another thread is setting up D3D12 at that moment.

**What happens then.** If the game's load succeeded, the module is set up right away when possible. Otherwise it is recorded, with one reference, and set up later: after DLL attach (the GPU info worker) and at every DXGI factory creation, until it works.

**Limits.**

- Only `d3d12.dll` is recovered this way. Redirects and blocks for other DLLs on a thread without TLS do not happen, and a handle already returned to the game is not changed.
- Up to 32 modules wait at once. A path longer than 1023 characters is not recognised.
- Images loaded with `DONT_RESOLVE_DLL_REFERENCES` are never set up, because their initialization did not run. OptiScaler remembers up to 64 of them, without a reference, until the game frees them. When the list is full, new ones are not remembered. The F5Low DLSS-G block still applies to these loads.
- Files loaded as data or resources are left alone.
