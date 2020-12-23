#!/bin/bash
cd build && cmake .. && make -j8 && cd ../
###################################################################
# Fill the variables DATASET_PATH, USE_REGULAR_VIO and PARALLEL_RUN.

# Specify path of the EuRoC dataset.
DATASET_PATH="/home/SENSETIME/wangshuai2/Desktop/slam/dataset/euroc/V1_01_easy"
#DATASET_PATH="/home/SENSETIME/wangshuai2/Desktop/slam/dataset/euroc/V1_02_medium"
#DATASET_PATH="/home/SENSETIME/wangshuai2/Desktop/slam/dataset/euroc/V1_03_difficult"

# Specify: 1 to use Regular VIO, 0 to use Normal VIO with default parameters.
USE_REGULAR_VIO=1

# Specify: 0 to run on EuRoC data, 1 to run on Kitti
DATASET_TYPE=0

# Specify: 1 to run pipeline in parallel mode, 0 to run sequentially.
PARALLEL_RUN=0

# Specify: 1 to log output files, 0 to not run logger
LOG_OUTPUT=0
###################################################################

# Parse Options.
if [ $# -eq 0 ]; then
  # If there is no options tell user what are the values we are using.
  echo "Using dataset at path: $DATASET_PATH"
  if [ $USE_REGULAR_VIO == 1 ]; then
    echo "Using REGULAR VIO."
  fi
else
  # Parse all the options.
  while [ -n "$1" ]; do # while loop starts
      case "$1" in
        # Option -p, provides path to dataset.
      -p) DATASET_PATH=$2
          echo "Using dataset at path: $DATASET_PATH"
          shift ;;
        # Option -d, set dataset type
      -d) DATASET_TYPE=$2
          echo "Using dataset type: $DATASET_TYPE"
          echo "0 is for euroc and 1 is for kitti"
          shift ;;
        # Option -r, specifies that we want to use regular vio.
      -r) USE_REGULAR_VIO=1
          echo "Using Regular VIO!" ;;
      -s) PARALLEL_RUN=0
          echo "Run VIO in SEQUENTIAL mode!" ;;
      -l) LOG_OUTPUT=1
          echo "Logging output!";;
      --)
          shift # The double dash which separates options from parameters
          break
          ;; # Exit the loop using break command
      *) echo "Option $1 not recognized" ;;
      esac
      shift
  done
fi

# No user input from this point on.
# Unless user specified to use Regular VIO, run pipeline with default parameters.
BACKEND_TYPE=0
VIO_PARAMS_PATH="../params/regularVioParameters.yaml"
TRACKER_PARAMS_PATH="../params/trackerParameters.yaml"
if [ $USE_REGULAR_VIO == 1 ]; then
  BACKEND_TYPE=1
  VIO_PARAMS_PATH="../params/regularVioParameters.yaml"
  TRACKER_PARAMS_PATH="../params/trackerParameters.yaml"
fi

# Change directory to parent path, in order to make this script
# independent of where we call it from.
parent_path=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )
cd "$parent_path"

echo """ Launching:

         ███████╗██████╗  █████╗ ██████╗ ██╗  ██╗    ██╗   ██╗██╗ ██████╗
         ██╔════╝██╔══██╗██╔══██╗██╔══██╗██║ ██╔╝    ██║   ██║██║██╔═══██╗
         ███████╗██████╔╝███████║██████╔╝█████╔╝     ██║   ██║██║██║   ██║
         ╚════██║██╔═══╝ ██╔══██║██╔══██╗██╔═██╗     ╚██╗ ██╔╝██║██║   ██║
         ███████║██║     ██║  ██║██║  ██║██║  ██╗     ╚████╔╝ ██║╚██████╔╝
         ╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝      ╚═══╝  ╚═╝ ╚═════╝

 """

# Execute stereoVIOEuroc with given flags.
# The flag --help will provide you with information about what each flag
# does.
../build/stereoVIOEuroc \
  --logtostderr=1 \
  --colorlogtostderr=1 \
  --log_prefix=0 \
  --dataset_path="$DATASET_PATH" \
  --vio_params_path="$VIO_PARAMS_PATH" \
  --initial_k=50 \
  --final_k=22913 \
  --tracker_params_path="$TRACKER_PARAMS_PATH" \
  --flagfile="../params/flags/stereoVIOEuroc.flags" \
  --flagfile="../params/flags/Mesher.flags" \
  --flagfile="../params/flags/VioBackEnd.flags" \
  --flagfile="../params/flags/RegularVioBackEnd.flags" \
  --flagfile="../params/flags/Visualizer3D.flags" \
  --flagfile="../params/flags/EthParser.flags" \
  --v=0 \
  --vmodule=VioBackEnd=0,RegularVioBackEnd=1,Mesher=0,StereoVisionFrontEnd=0 \
  --backend_type="$BACKEND_TYPE" \
  --parallel_run="$PARALLEL_RUN" \
  --dataset_type="$DATASET_TYPE" \
  --log_output="$LOG_OUTPUT" \
  --output_path="../output_logs/"
