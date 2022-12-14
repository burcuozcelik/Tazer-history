#!/bin/bash
module purge
module load gcc/8.1.0

#---------------CHANGE ME!!!!---------------------
# this should point to the install dir for tazer
export TAZER_ROOT=/people/mutl832/tazer-july/
export TAZER_BUILD_DIR=/people/mutl832/tazer-july/build/
WORKLOADSIM_PATH=${HOME}/tazer-bigflowsim/workloadSim
#--------------------------------------------

#----------------EXP PARAMETERS--------------
exp_type="E" #A: test1 , B: test2 linear, C: test2 random, D:Test1+test2random, E:test1+test2linear+test2random 
scalable=1
shared=1
filemem=1
private_size=8 #in MB
shared_size=32 #in MB
block_size=128 #in KB

#-------------------------------------------

#---------------CLEAN UP--------------------
rm ./*.meta.in
rm server.log
rm slurm*
rm tazer_output*
#-------------------------------------------

folderprefix="Results_"
mkdir ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/
cd ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/
rm ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/*.meta.in
rm ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/*.sh
rm ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/*.out
rm ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/*.txt
rm ${TAZER_ROOT}/script/Experiment3/${folderprefix}${exp_type}_${scalable}_${shared}_${filemem}/*.log
rm -rf ./FileCache/

cp ${TAZER_ROOT}/script/Experiment3/launch_server.sh ./
cp ${TAZER_ROOT}/script/Experiment3/launch_exp.sh ./

TAZER_LIB_PATH=${TAZER_BUILD_DIR}src/client/libclient.so
SERVER_PATH=${TAZER_BUILD_DIR}src/server/server
SERVER_PORT=6024
CLOSE_SERVER=${TAZER_BUILD_DIR}test/CloseServer

server_command="sbatch -A oddite -N1 -x node42,node12 --parsable ./launch_server.sh"
tazer_server_task_id=$(${server_command})
sleep 10

tazer_server_nodes=`squeue -j ${tazer_server_task_id} -h -o "%N"`
echo "server node: "$tazer_server_nodes

client_command="sbatch -A oddite -N1 --exclude=node42 --parsable ./launch_exp.sh ${exp_type} ${tazer_server_nodes} ${TAZER_ROOT} ${TAZER_BUILD_DIR} ${WORKLOADSIM_PATH} ${scalable} ${shared} ${filemem} ${private_size} ${shared_size} ${block_size}"
# echo $client_command
client_task_id=$(${client_command}) 
sleep 2
# client_node=`squeue -j ${client_task_id} -h -o "%N"`
# echo "client node: "$client_node


salloc -A oddite -N1 -d afterany:${client_task_id} ${CLOSE_SERVER} ${tazer_server_nodes} 6024
echo ${CLOSE_SERVER} ${tazer_server_nodes} 6024

wait