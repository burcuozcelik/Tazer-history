Downloads:

1. make sure you are in script/public_workload/ 

2. wget https://zenodo.org/record/3340631/files/IOnode_traces.tgz?download=1

3. mv IOnode_traces.tgz?download=1 IOnode_traces.tgz

4. tar -xvf IOnode_traces.tgz

Run the code:

python transform.py <Experiment ID>

ex: python transform.py 216 