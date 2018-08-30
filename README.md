# HTTP_proxy
Multi-threaded HTTP 1.0 web proxy server that can handle multiple HTTP requests and browser connections. 

### Summary
Upon a new browser connection, the server:
Receives headers from client
Sanitizes and parses the absolute URI
Forwards the modified request to the origin server
Returns the response to client 

### To compile
`g++ -lpthread -std= c++11 proxy.cpp -o proxy`
or
`make`

### To run
`./proxy [portNumber]`

### Notes:
 - Does not return 500 Internal Error when encountering a request that is not GET (e.g. POST, PUT, HEAD).
 - Was tested with a configured Firefox browser for HTTP/1.0 on the following sites:
    - www.ikea.com
    - www.cnn.com
    - www.pbs.org
    - www.espn.com
