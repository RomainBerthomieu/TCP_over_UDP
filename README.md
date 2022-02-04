# TCP_over_UDP
In this project, we must use UDP to mimick TCP. We chose C as we want the best performances in this time-sensitive application.
The objective was, for a specific client, to have the best throughput. Each server is therefore client-specific, and can have major difficulties and clients with different networks/behaviors.

# Usage
- make binaries from c files with "make"
- set up the server with "./serverX-SP33DRUNN3RS {port_number}"
- start client with "./clientX {IP_server} {port_number} {file_name}" (add " 0" to quit debug mode) 

# NB
- the file to be transmitted must be in the same directory as the server

# To-Do
- implement NewReno
- play around with the NewReno parameters
- tidy and comment the code
- make some graphs with WireShark
