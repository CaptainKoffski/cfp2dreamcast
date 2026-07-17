# The idea
Cleopatra Fortune Plus is a Sega Naomi title. Sega Naomi is a platform architecturally close to Sega Dreamcast, but more powerful (mostly in RAM).

# The goal
Port the Cleopatra Fortune Plus game to Dreamcast with AI heavylifting. The human (me) knows nothing about solving this type of tasks and can help with only routine things (like app installation).

# Existing Naomi ports
There are some Sega Naomi ports to Dreamcast; however, all of them, as far as I know, are made by the original game creators, having access to their own source code. The community has no successful cases of Sega Naomi ports, as far as I know. But we have similar successful cases with another platform, Atomiswave.

# Atomiswave ports
There is another Dreamcast-like arcade plarform - Atomiswave. It is less powerful, than Sega Naomi. Romhackers were able to port almost full library of Atomiswave games. It can be an additional reference for this project.

# Tooling
I don't know which tooling do we need here, I need Claude Code's opinion on that. I'm ready to install anything required. Moreover, if a tool is open source and requires improvement (say, add AI-readable additional debug info), such update is in scope of this project, I can clone the tool and dispatch an agent for such improvement.

# Data collecting
Please save to the disc all the context you have, so the future AI agents will now how to do their job.
As for the Sega Naomi architecture discrepancies and any other auxiliary info, I need Claude Code to collect it by itself and save it.
If we need to gut another project (say, MAME), in order to better understand the way Sega Naomi works, I'm OK with such deep diving, just say me and I will clone everything we need.

# Necessary simplifications
Having that Sega Naomi is superior than Dreamcast, and it mostly in RAM, it is almost inevitable to cut something. Compress or decrease textures, music, etc. Think thoroughly what can we compact/change with minimal effect on the game. As we see from the existing official Sega Naomi ports, they were made with such little amount of changes, so it is relatively hard to distinguish Dreamcast versions from their corresponding Sega Naomi originals.

# The Naomi rom
`Cleopatra Fortune Plus.dat`
