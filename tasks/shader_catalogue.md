# Audio

## 6. Audio-driven fluid vortex (mid)
FFT data drives the velocity field of a 2D incompressible fluid simulation (Jos Stam stable fluids). Bass bins inject large-scale curl vortices; treble drives fine viscous streaks. Dye advects through the field each frame.
Parameters: viscosity, dyeDiffusion, bassInfluenceScale, trebleScale, injectionPoints, colourByVorticity

## 8. Synaptic fire network (wild)
A procedurally placed graph of nodes (configurable count, Watts-Strogatz topology). Audio amplitude triggers action-potential cascades: nodes charge up, fire when threshold exceeded, and enter refractory period. Axon rendering shows propagation delay. Sustained tones lock the network into oscillatory attractors.
Parameters: nodeCount, kNeighbours, rewiringProb, firingThreshold, refractoryMs, propagationDelay, spontaneousRate

## 9. Fourier crystal growth (wild)
Crystals nucleate at seed points and grow outward. Crystal symmetry order (2 to 12-fold) is mapped to frequency bins: bin 0 = hexagonal, bin N = dodecagonal. Lattice spacing scales with amplitude; saturation encodes energy. Generates structures resembling actual mineralogical habits from Fm3m to P6/mmm.
Parameters: symmetryMapping, nucleationRate, growthSpeed, maxRadius, latticeSaturation, displaciveNoise

## 10. Psychoacoustic topography (wild)
Treats the FFT as a height map defining a terrain surface. Mesh subdivided to FFT resolution. Camera orbits continuously; transients cause volcanic eruption events; bass defines plateau regions; sustained high harmonics erode ridgelines. Lighting is directional with specular.
Parameters: meshResolution, heightScale, erosionRate, cameraOrbitSpeed, lightAzimuth, eruptionThreshold, wireframe

# Generative

## 11. Perlin flow field (mundane)
Particles follow a 2D velocity field defined by a Perlin noise curl function. Octave-summed noise gives fine structure at high lacunarity. Particles leave a trail that fades over time.
Parameters: particleCount, noiseScale, octaves, persistence, lacunarity, trailLength, stepSize, colourByAngle

## 12. Conway Game of Life (chroma age) (mundane)
Standard B3/S23 automaton, but cells retain full HSV colour history. Hue encodes age (birth = red, long-lived = blue-violet); saturation encodes neighbourhood activity. Accepts arbitrary outer-totalistic rule strings.
Parameters: ruleString, cellSize, wrapMode, updateHz, initialDensity, ageSaturation

## 15. Physarum slime mould (mid)
Agent-based Physarum polycephalum transport network simulation. Agents sense chemoattractant, rotate toward maxima, and deposit trail. Trail diffuses and decays. Self-organising networks form, matching the Tero et al. (2010) topology formation results.
Parameters: agentCount, sensorAngle, sensorDist, rotationAngle, depositAmount, decayRate, diffusionKernel, attractorPoints

## 17. Diffusion-limited aggregation (mid)
Particles perform random walks and irreversibly attach to a growing cluster on contact. Produces statistically self-similar fractal structures with Hausdorff dimension ~1.71 in 2D. Supports directional drift bias producing asymmetric DLA.
Parameters: particleCount, stickingProb, driftAngle, driftStrength, seed, walkerSource, colourByRadius

## 18. Arthropod cuticle patterning (wild)
Two-component Meinhardt-Gierer activator-inhibitor system parameterised to reproduce arthropod cuticle pigmentation (striped, spotted, and reticulate regimes matching empirical crustacean and insect patterns). Body plan geometry is configurable: annelid cylinder, crustacean carapace, lepidopteran wing, insect abdomen. Parameter sets labelled by clade.
Parameters: activatorDiff, inhibitorDiff, activationRate, inhibitionRate, bodyGeometry, segmentCount, pigmentChannels, cladePreset

## 20. Hele-Shaw viscous fingering (wild)
Laplacian growth model of a less-viscous fluid displacing a more-viscous one in a thin cell. Interface instability produces fractal fingertip branching (Saffman-Taylor instability). Surface tension parameter stabilises at a characteristic wavelength.
Parameters: viscosityRatio, surfaceTension, injectionRate, noiseAmplitude, gridResolution, formulation, colourByPressure

## 21. Electromagnetic field lines (wild)
N point charges on the canvas with configurable sign and magnitude. Field lines traced by numerical integration of the vector field. Equipotential contours computed separately. Toggle between E-field, B-field (moving charges), force vectors, and flux density heatmap.
Parameters: chargeCount, chargeSignMode, lineCount, integrationStep, displayMode, colourByMagnitude, fieldFalloffExp

# Video

## 22. Colour grading (3-way) (mundane)
Lift, gamma, and gain controls per RGB channel plus global saturation, hue rotation, and vignette. Implements the standard DaVinci-style 3-way corrector. 
Parameters: liftR, liftG, liftB, gammaR, gammaG, gammaB, gainR, gainG, gainB, saturation, hueRotate, vignetteStrength

## 27. Oil paint filter (EWA) (mid)
Structure-tensor-guided anisotropic smoothing: computes local gradient orientation, applies elliptic weighted average filter aligned to flow direction. Multi-scale Kuwahara variant for sectoral variance minimisation. Produces oil-paint appearance without artefacts at edges.
Parameters: smoothingRadius, orientationSigma, sectorCount, sharpStrength, colourSatBoost, iterations

## 28. Slit-scan temporal splice (mid)
Accumulates a configurable-width vertical (or horizontal, or rotated) slice from each frame into a scroll buffer, building a compound image where the spatial axis encodes time. Classic streak-photo effect. Configurable slice width, scroll direction, and temporal compression ratio.
Parameters: sliceWidth, sliceX, scrollAxis, bufferLength, temporalCompression, blendMode, colourMap

## 29. Fourier domain sculpting (wild)
Performs a spatial FFT of each frame, exposes the magnitude and phase spectrum as an interactive canvas where you paint masks (low-pass disc, annular band-pass, wedge directional, notch filter). IFFT reconstructs. Makes spatial frequency content directly manipulable as a visual parameter.
Parameters: maskType, cutoffFreq, bandWidth, notchAngle, notchWidth, phaseRandomise, magnitudeGamma, showSpectrum

## 30. Datamosh block drift (wild)
Extracts block motion vectors from the video stream, then applies those vectors to a frame buffer lagged by N frames (decoupling appearance from motion). Blocks accumulate displacement over multiple frames before partial refresh. Replicates datamoshing codec artefacts without actual codec involvement.
Parameters: blockSize, vectorScale, lagFrames, refreshRate, blendWeight, iFrameInterval, errorDiffusion

## 32. Non-Euclidean spacetime lens (wild)
Applies a Riemannian metric warp to image sampling coordinates. Positive curvature (spherical metric) compresses objects toward edges and inverts them at the antipode. Negative curvature (hyperbolic metric) produces exponential edge stretching analogous to Escher's Circle Limit series, applied dynamically to live video. Configurable curvature sign and magnitude, optical centre, and animated geodesic drift.
Parameters: curvature, opticalCentre, driftSpeed, driftAngle, metric, samplingMode, animateCurvature