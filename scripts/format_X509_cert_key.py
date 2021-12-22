# Python script to convert certificate/key file to string format that can be used in C source files.
#
# Usage:
#   python format_cert_key.py <one-or-more-file-name-of-certificate-or-key-with-extension>
#
# Example:
#   python format_cert_key.py mosquitto_ca.crt mosquitto_client.crt mosquitto_client.key
#
import sys

#Function that adds a new line character and trailing backslash except on the final line
def format_file(f):
    with open(f, 'r') as fd:
        lines = fd.read().splitlines()
        line_num = 0
        for i in lines:
            i = "\""+i+"\\n\""
            line_num = line_num + 1
            print(i)

#Main function. Execution starts here
if __name__ == '__main__':

    for arg in (sys.argv):
        if (arg.endswith(".crt") or arg.endswith(".pem") or arg.endswith(".key")):
            print("String format of",arg,"file:")
            format_file(arg)
            print("")
        else:
            if (arg.endswith(".py") == False):
                print("Pass file with extension (*.crt) (*.pem) or (*.key) only!")
            
    input("Enter any key to exit...")
