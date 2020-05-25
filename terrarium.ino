#define null 0
//#define DEBUG_COMMS 1

#define COLOR_DRIPPER makeColorRGB(  0, 255, 128)
#define COLOR_DIRT    makeColorRGB(107,  80,   0)
#define COLOR_BUG     makeColorRGB(107,  80,   0)
#define COLOR_WATER   makeColorRGB(  0,   0, 128)
#define COLOR_WATER_FULL makeColorRGB(0,  128, 128)

#define COLOR_DEBUG1  makeColorRGB(255, 255, 0)
#define COLOR_DEBUG2  makeColorRGB(255, 0, 255)
#define COLOR_DEBUG3  makeColorRGB(0, 255, 255)

#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#define OPPOSITE_FACE(f) (((f) < 3) ? ((f) + 3) : ((f) - 3))

// Gravity is a constant velocity for now (not accelerative)
// Might change that later if there's need
#define GRAVITY 200 // ms to fall from pixel to pixel
Timer gravityTimer;

// States the player can select for a tile
// They cycle through by clicking the tile's button
enum eTileState
{
  kTileState_Empty,
  kTileState_Dripper,

  kTileState_MAX
};

// New tile states can be discovered and enabled given certain conditions
// -1 = the state can always be selected
// 0 = the state is disabled
// 1+ = the state has this many uses
char tileStateEnables[]
{
  -1,   // Empty
  -1,   // Dripper
  
  0     // MAX
};

// Current tile state - starts empty
char tileState = kTileState_Empty;

// -------------------------------------------------------------------------------------------------
// DRIPPER/WATER
//
#define DRIPPER_AMOUNT 4
#define DRIPPER_RATE 500   // ms between drips
bool hasDripper = false;
Timer dripperTimer;

#define MAX_WATER_LEVEL 15 // water level can actually go above this, but will stop accepting new water

// -------------------------------------------------------------------------------------------------
//
// COMMUNICATIONS
//
// -------------------------------------------------------------------------------------------------

#define TOGGLE_COMMAND 1
#define TOGGLE_DATA 0
struct FaceValue
{
  byte value : 4;
  byte toggle : 1;
  byte ack : 1;
};

enum NeighborSyncFlag
{
  NeighborSyncFlag_Present    = 1<<0,
  NeighborSyncFlag_GotWater   = 1<<1,
  NeighborSyncFlag_SentWater  = 1<<2,
};

struct FaceState
{
  FaceValue faceValueIn;
  FaceValue faceValueOut;
  byte lastCommandIn;

  byte neighborSyncFlags;
  
  byte waterLevel;
  byte waterLevelNew;
  byte waterLevelNeighbor;
  byte waterLevelNeighborNew;

#if DEBUG_COMMS
  byte ourState;
  byte neighborState;
#endif
};
FaceState faceStates[FACE_COUNT];

#define NeighborSynced(f) ((faceStates[f].neighborSyncFlags & \
                              (NeighborSyncFlag_Present | NeighborSyncFlag_GotWater)) == \
                              (NeighborSyncFlag_Present | NeighborSyncFlag_GotWater))

enum CommandType
{
  CommandType_None,         // no data
  CommandType_WaterLevel,   // data=value
  CommandType_WaterAdd,     // data=value
#if DEBUG_COMMS
  CommandType_UpdateState,
#endif
  
  CommandType_MAX
};

struct CommandAndData
{
  CommandType command : 4;
  byte data : 4;
};

#define COMM_QUEUE_SIZE 4
CommandAndData commQueues[FACE_COUNT][COMM_QUEUE_SIZE];

#define COMM_INDEX_ERROR_OVERRUN 0xFF
#define COMM_INDEX_OUT_OF_SYNC   0xFE
byte commInsertionIndexes[FACE_COUNT];

#define ErrorOnFace(f) (commInsertionIndexes[f] > COMM_QUEUE_SIZE)


#if DEBUG_COMMS
// Timer used to toggle between green & blue
Timer sendNewStateTimer;
#endif


// =================================================================================================
//
// SETUP
//
// =================================================================================================

void setup()
{
  resetOurState();
  FOREACH_FACE(f)
  {
    resetCommOnFace(f);
  }
  gravityTimer.set(GRAVITY);
}

// =================================================================================================
//
// COMMUNICATIONS
// Cut-and-paste from sample project
//
// =================================================================================================

void resetCommOnFace(byte f)
{
  // Clear the queue
  commInsertionIndexes[f] = 0;

  FaceState *faceState = &faceStates[f];

  // Put the current output into its reset state.
  // In this case, all zeroes works for us.
  // Assuming the neighbor is also reset, it means our ACK == their TOGGLE.
  // This allows the next pair to be sent immediately.
  // Also, since the toggle bit is set to TOGGLE_DATA, it will toggle into TOGGLE_COMMAND,
  // which is what we need to start sending a new pair.
  faceState->faceValueOut.value = 0;
  faceState->faceValueOut.toggle = TOGGLE_DATA;
  faceState->faceValueOut.ack = TOGGLE_DATA;
  sendValueOnFace(f, faceState->faceValueOut);

  // Clear sync flags dealing with our state
  faceState->neighborSyncFlags &= ~NeighborSyncFlag_SentWater;
}

void sendValueOnFace(byte f, FaceValue faceValue)
{
  byte outVal = *((byte*)&faceValue);
  setValueSentOnFace(outVal, f);
}

// Called by the main program when this tile needs to tell something to
// a neighbor tile.
void enqueueCommOnFace(byte f, CommandType commandType, byte data)
{
  if (commInsertionIndexes[f] >= COMM_QUEUE_SIZE)
  {
    // Buffer overrun - might need to increase queue size to accommodate
    commInsertionIndexes[f] = COMM_INDEX_ERROR_OVERRUN;
    return;
  }

  byte index = commInsertionIndexes[f];
  commQueues[f][index].command = commandType;
  commQueues[f][index].data = data;
  commInsertionIndexes[f]++;
}

// Called every iteration of loop(), preferably before any main processing
// so that we can act on any new data being sent.
void updateCommOnFaces()
{
  FOREACH_FACE(f)
  {
    // Is the neighbor still there?
    if (isValueReceivedOnFaceExpired(f))
    {
      // Lost the neighbor - no longer in sync
      resetCommOnFace(f);
      faceStates[f].neighborSyncFlags = 0;
      continue;
    }

    // If there is any kind of error on the face then do nothing
    // The error can be reset by removing the neighbor
    if (ErrorOnFace(f))
    {
      continue;
    }

    FaceState *faceState = &faceStates[f];

    faceState->neighborSyncFlags |= NeighborSyncFlag_Present;

    // Read the neighbor's face value it is sending to us
    byte val = getLastValueReceivedOnFace(f);
    faceState->faceValueIn = *((FaceValue*)&val);
    
    //
    // RECEIVE
    //

    // Did the neighbor send a new comm?
    // Recognize this when their TOGGLE bit changed from the last value we got.
    if (faceState->faceValueOut.ack != faceState->faceValueIn.toggle)
    {
      // Got a new comm - process it
      byte value = faceState->faceValueIn.value;
      if (faceState->faceValueIn.toggle == TOGGLE_COMMAND)
      {
        // This is the first part of a comm (COMMAND)
        // Save the command value until we get the data
        faceState->lastCommandIn = value;
      }
      else
      {
        // This is the second part of a comm (DATA)
        // Use the saved command value to determine what to do with the data
        switch (faceState->lastCommandIn)
        {
          case CommandType_WaterLevel:
            faceState->waterLevelNeighbor = value;
            faceState->neighborSyncFlags |= NeighborSyncFlag_GotWater;
            break;
          case CommandType_WaterAdd:
            faceState->waterLevel += value;
            replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceState->waterLevel);
            break;
#if DEBUG_COMMS
          case CommandType_UpdateState:
            faceState->ourState = value;
            break;
#endif
        }
      }

      // Acknowledge that we processed this value so the neighbor can send the next one
      faceState->faceValueOut.ack = faceState->faceValueIn.toggle;
    }
    
    //
    // SEND
    //
    
    // Did the neighbor acknowledge our last comm?
    // Recognize this when their ACK bit equals our current TOGGLE bit.
    if (faceState->faceValueIn.ack == faceState->faceValueOut.toggle)
    {
      // If we just sent the DATA half of the previous comm, check if there 
      // are any more commands to send.
      if (faceState->faceValueOut.toggle == TOGGLE_DATA)
      {
        if (commInsertionIndexes[f] == 0)
        {
          // Nope, no more comms to send - bail and wait
          continue;
        }
      }

      // Send the next value, either COMMAND or DATA depending on the toggle bit

      // Toggle between command and data
      faceState->faceValueOut.toggle = ~faceState->faceValueOut.toggle;
      
      // Grab the first element in the queue - we'll need it either way
      CommandAndData commandAndData = commQueues[f][0];

      // Send either the command or data depending on the toggle bit
      if (faceState->faceValueOut.toggle == TOGGLE_COMMAND)
      {
        faceState->faceValueOut.value = commandAndData.command;
      }
      else
      {
        faceState->faceValueOut.value = commandAndData.data;
  
        // No longer need this comm - shift everything towards the front of the queue
        for (byte commIndex = 1; commIndex < COMM_QUEUE_SIZE; commIndex++)
        {
          commQueues[f][commIndex-1] = commQueues[f][commIndex];
        }

        // Adjust the insertion index since we just shifted the queue
        if (commInsertionIndexes[f] == 0)
        {
          // Shouldn't get here - if so something is funky
          commInsertionIndexes[f] = COMM_INDEX_OUT_OF_SYNC;
          continue;
        }
        else
        {
          commInsertionIndexes[f]--;
        }
      }
    }
  }

  FOREACH_FACE(f)
  {
    // Update the value sent in case anything changed
    sendValueOnFace(f, faceStates[f].faceValueOut);
  }
}

// =================================================================================================
//
// LOOP
//
// =================================================================================================

void loop()
{
  // User input
  updateUserSelection();

  // Update neighbor presence and comms
  updateCommOnFaces();

#if DEBUG_COMMS
  if (sendNewStateTimer.isExpired())
  {
    FOREACH_FACE(f)
    {
      byte nextVal = faceStates[f].neighborState == 2 ? 3 : 2;
      faceStates[f].neighborState = nextVal;
      enqueueCommOnFace(f, CommandType_UpdateState, nextVal);
    }
    sendNewStateTimer.set(500);
  }
#else

  // Systems updates
  loopWater();
  // TODO : MOAR

  // Update water levels and such
  postProcessState();
#endif

  // Set the colors on all faces according to what's happening in the tile
  render();
}

// =================================================================================================
//
// GENERAL
// Non-system-specific functions.
//
// =================================================================================================

void updateUserSelection()
{
  if (buttonSingleClicked())
  {
    // User state changed

    char prevTileState = tileState;
    
    do
    {
      tileState++;
      if (tileState >= kTileState_MAX)
      {
        tileState = 0;
      }
    } while (tileStateEnables[tileState] == 0);  // skip any disabled tile states

    // Reset our state and tell our neighbors to reset their perception of us
    resetOurState();

    if (prevTileState != tileState)
    {
      switch (prevTileState)
      {
        case kTileState_Dripper:
          hasDripper = false;
          break;
      }

      switch (tileState)
      {
        case kTileState_Dripper:
          hasDripper = true;
          dripperTimer.set(DRIPPER_RATE);   // start dripping
          break;
      }
    }
  }  // buttonSingleClicked
}

void resetOurState()
{
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];

    faceState->waterLevel = 0;
    faceState->waterLevelNew = 0;
    
    // Clear sync flags dealing with our state
    faceState->neighborSyncFlags &= ~NeighborSyncFlag_SentWater;
  }
}

// Replace the data for the given command in the queue, if it exists.
// Otherwise add it to the queue.
void replaceOrEnqueueCommOnFace(byte f, CommandType commandType, byte data)
{
  for (byte index = 0; index < commInsertionIndexes[f]; index++)
  {
    if (commQueues[f][index].command == commandType)
    {
      commQueues[f][index].data = data;
      return;
    }
  }

  enqueueCommOnFace(f, commandType, data);
}

void postProcessState()
{
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];
    
    if (faceState->waterLevelNew > 0)
    {
      faceState->waterLevel += faceState->waterLevelNew;
      faceState->waterLevelNew = 0;

      if (NeighborSynced(f))
      {
        replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceState->waterLevel);
      }
    }

    if (faceState->waterLevelNeighborNew > 0)
    {
      if (NeighborSynced(f))
      {
        replaceOrEnqueueCommOnFace(f, CommandType_WaterAdd, faceState->waterLevelNeighborNew);
      }

      // Temporarily modify our understanding of the neighbor water level until they actually update us
      faceState->waterLevelNeighbor += faceState->waterLevelNeighborNew;
      faceState->waterLevelNeighborNew = 0;
    }

    // If we haven't sent our water level then do that now
    if (faceState->neighborSyncFlags & NeighborSyncFlag_Present)
    {
      if (!(faceState->neighborSyncFlags & NeighborSyncFlag_SentWater))
      {
        replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceState->waterLevel);
        faceState->neighborSyncFlags |= NeighborSyncFlag_SentWater;
      }
    }
  }

  if (gravityTimer.isExpired())
  {
    gravityTimer.set(GRAVITY);
  }
}

// =================================================================================================
//
// WATER/DRIPPER
//
// =================================================================================================

void loopWater()
{  
  if (hasDripper)
  {
    if (dripperTimer.isExpired())
    {
      dripperTimer.set(DRIPPER_RATE);
      if (faceStates[0].waterLevel < MAX_WATER_LEVEL)
      {
        byte waterToAdd = MAX_WATER_LEVEL - faceStates[0].waterLevel;
        waterToAdd = MIN(waterToAdd, DRIPPER_AMOUNT);
        faceStates[0].waterLevel += waterToAdd;
      }
    }
  }

  // Make water fall/flow
  if (gravityTimer.isExpired())
  {
    // Water falls straight down within a tile, if possible.
    // If the neighbor below is full/blocked then flow to the sides.

    // FALL DOWN
    // Bottom row first (to make room for the top row)
    if (NeighborSynced(3))
    {
      waterFallDown(3, &faceStates[3].waterLevelNeighbor, &faceStates[3].waterLevelNeighborNew);
    }
    // Top row last
    waterFallDown(0, &faceStates[3].waterLevel, &faceStates[3].waterLevelNew);
    waterFallDown(1, &faceStates[2].waterLevel, &faceStates[2].waterLevelNew);
    waterFallDown(5, &faceStates[4].waterLevel, &faceStates[4].waterLevelNew);

    // FLOW TO SIDES
    waterFlowToSide(5,
                    &faceStates[0].waterLevel,
                    &faceStates[0].waterLevelNew,
                    NeighborSynced(5) ? &faceStates[5].waterLevelNeighbor : null,
                    &faceStates[5].waterLevelNeighborNew);
    waterFlowToSide(4,
                    &faceStates[3].waterLevel,
                    &faceStates[3].waterLevelNew,
                    NeighborSynced(4) ? &faceStates[4].waterLevelNeighbor : null,
                    &faceStates[4].waterLevelNeighborNew);
    waterFlowToSide(0,
                    &faceStates[5].waterLevel,
                    &faceStates[5].waterLevelNew,
                    &faceStates[1].waterLevel,
                    &faceStates[1].waterLevelNew);
    waterFlowToSide(3,
                    &faceStates[4].waterLevel,
                    &faceStates[4].waterLevelNew,
                    &faceStates[2].waterLevel,
                    &faceStates[2].waterLevelNew);
    waterFlowToSide(1,
                    &faceStates[0].waterLevel,
                    &faceStates[0].waterLevelNew,
                    NeighborSynced(1) ? &faceStates[1].waterLevelNeighbor : null,
                    &faceStates[1].waterLevelNeighborNew);
    waterFlowToSide(2,
                    &faceStates[3].waterLevel,
                    &faceStates[3].waterLevelNew,
                    NeighborSynced(2) ? &faceStates[2].waterLevelNeighbor : null,
                    &faceStates[2].waterLevelNeighborNew);
  }
}

void waterFallDown(char srcPixel, byte *dst, byte *dstNew)
{
  if (faceStates[srcPixel].waterLevel <= 0)
  {
    return;   // no water here to fall
  }

  byte dstTotal = *dst + *dstNew;
  if (dstTotal >= MAX_WATER_LEVEL)
  {
    return;   // no room in destination to accept more water
  }

  // All water falls, if possible, up to the maximum the destination can hold
  int waterAmount = MIN(MAX_WATER_LEVEL - dstTotal, faceStates[srcPixel].waterLevel);
  
  // Move the water!
  faceStates[srcPixel].waterLevel -= waterAmount;
  *dstNew += waterAmount;

  // The source water level changed - notify its neighbor
  replaceOrEnqueueCommOnFace(srcPixel, CommandType_WaterLevel, faceStates[srcPixel].waterLevel);
}

void waterFlowToSide(char srcPixel, byte *dst1, byte *dst1New, byte *dst2, byte *dst2New)
{
  // ASSUMPTION : All water has fallen down that was able to.
  //              This means that any remaining water can flow to the side.
  byte srcVal = faceStates[srcPixel].waterLevel;
  
  if (srcVal == 0)
  {
    return;   // no water here to flow
  }
  
  char dst1Total = *dst1 + *dst1New;
  if (dst1Total < MAX_WATER_LEVEL)
  {
    // Give half our water to either side, up to the max the destination can hold
    char waterAmount = MIN(srcVal >> 1, MAX_WATER_LEVEL - dst1Total);
    *dst1New += waterAmount;
    faceStates[srcPixel].waterLevel -= waterAmount;
    
    // The source water level changed - notify its neighbor
    replaceOrEnqueueCommOnFace(srcPixel, CommandType_WaterLevel, faceStates[srcPixel].waterLevel);
  }

  if (dst2 != null)
  {
    char dst2Total = *dst2 + *dst2New;
    if (dst2Total < MAX_WATER_LEVEL)
    {
      // Give half our water to either side, up to the max the destination can hold
      char waterAmount = MIN(srcVal >> 1, MAX_WATER_LEVEL - dst2Total);
      *dst2New += waterAmount;
      faceStates[srcPixel].waterLevel -= waterAmount;
      
      // The source water level changed - notify its neighbor
      replaceOrEnqueueCommOnFace(srcPixel, CommandType_WaterLevel, faceStates[srcPixel].waterLevel);
    }
  }
}

// =================================================================================================
//
// RENDER
//
// =================================================================================================

void render()
{
  setColor(OFF);

  // Show the base tile state first
  switch(tileState)
  {
    case kTileState_Empty:
      // already set tiles to black - don't need to do anything else
      //setColorOnFace(dim(WHITE, 64), 1);
      break;
      
    case kTileState_Dripper:
      // Intensity rises as it gets closer to dripping
      {
        long dimnessL = (dripperTimer.getRemaining() << 8) / DRIPPER_RATE;
        byte dimness = 255;
        if (dimnessL < 256)
        {
          dimness = 255 - dimnessL;
        }
        dimness = dimness >> 1;
        setColorOnFace(dim(COLOR_DRIPPER, dimness), 0);
      }
      break;
  }

  FOREACH_FACE(f)
  {
    if (faceStates[f].waterLevel > 0)
    {
      byte dimness = faceStates[f].waterLevel << 4;    // water level 1 = brightness 16
      Color waterColor = dim(COLOR_WATER, dimness);
      if (faceStates[f].waterLevel >= MAX_WATER_LEVEL)
      {
        // Special case for full water just so we can see it
        waterColor = COLOR_WATER_FULL;
      }
      setColorOnFace(waterColor, f);
    }
  }

  // Error codes
  FOREACH_FACE(f)
  {
    if (ErrorOnFace(f))
    {
      setColorOnFace(makeColorRGB(255,0,0), f);
    }
  }

#if DEBUG_COMMS
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];

    if (ErrorOnFace(f))
    {
      if (commInsertionIndexes[f] == COMM_INDEX_ERROR_OVERRUN)
      {
        setColorOnFace(MAGENTA, f);
      }
      else if (commInsertionIndexes[f] == COMM_INDEX_OUT_OF_SYNC)
      {
        setColorOnFace(ORANGE, f);
      }
      else
      {
        setColorOnFace(RED, f);
      }
    }
    else if (!isValueReceivedOnFaceExpired(f))
    {
      if (faceState->ourState == 2)
      {
        setColorOnFace(GREEN, f);
      }
      else if (faceState->ourState == 3)
      {
        setColorOnFace(BLUE, f);
      }
    }
  }
#endif

}
