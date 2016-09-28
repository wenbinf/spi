SecSTAR
=======

Visualize system structure and security property. There are two components:
- SecSTAR/SecSTAR: Python scripts (require: pydot module)
  - input: xml trace data in SecSTAR/traces
  - output: svg files
- SecSTAR/html: Javascript to animate svg files
  - The tricky part here is how to load all svg files efficiently. The solution is to load a zip file, then unzip it in browser and split all svgs.

Demo
----

[SecSTAR Demo for animating Condor's architecture](http://research.cs.wisc.edu/mist/projects/SecSTAR/)

Paper
-----

__Wenbin Fang__ et al. [Automated Tracing and Visualization of Software Security
Structure and Properties](http://research.cs.wisc.edu/mist/papers/Wenbin12SecSTAR.pdf)
