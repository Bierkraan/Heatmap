# import necessary libraries and functions 
from flask import Flask, request # type: ignore
from scipy.optimize import minimize # type: ignore
import numpy as np
from datetime import datetime
import json

# creating a Flask app 
app = Flask(__name__) 

# Block name, length square side, X offset, Y offset, sensors in block (top left, top right, bottom left, bottom right)
blocks = [
		["A", 10.0, 0, 0, [1001, 1002, 1003, 1004]],
		["B", 10.0, 10, 10, [1005, 1006, 1007, 1008]],
	]

path = 'devices.json'
allMacs = []
uniqueMacs = []

# on the terminal type: curl http://127.0.0.1:5000/ 
# returns hello world when we use GET. 
# returns the data that we send when we use POST. 
@app.route('/', methods = ['GET', 'POST']) 
def home():
	if(request.method == 'GET'):
		return "Hello World!"

@app.route('/current-time', methods = ['GET']) 
def current_time():
	# print(datetime.now().strftime('%H:%M.%S'))
	return datetime.now().strftime('%H:%M.%S')	
	
@app.route('/json-post', methods = ['POST']) 
def save_json():
	if request.get_json() is None:
		print("Error receiving POST!")
		return "Nothing received"
	else:
		request_data = request.get_json()

		# Get current POST scannerID
		for item in request_data:
			scanner_id = item.get('scannerID')
			break
		print("Received POST from scanner", scanner_id)
		allMacs = []
		uniqueMacs = []
		
		# Read current file
		with open(path, "r") as f:
			data = json.load(f)
		
		# Replace current file with everything except current scannerID
		with open(path, "w") as f:
			f.write("[\n")
			for item in data:
				allMacs.append(item["mac"])
				if not scanner_id in item["scannerID"]:
					json.dump(item, f)
					f.write(",\n")

		# Write current scanner data and calculate distance from RSSI
		with open(path, "a") as f:
			entry = 0
			for item in request_data:
				entry += 1
				P_t = -20 	# Transmit power in dBm
				n = 4.0 	# Path loss exponent
				L_f = 3		# Fixed losses in dB

				# RSSI reading
				P_r = item["rssi"]
				
				# Calculate distance
				distance = 10 ** ((P_t - P_r - L_f) / (10 *n))
				item["distance"] = distance

				allMacs.append(item["mac"])

				json.dump(item, f) # Write the updated item dictionary to our file
				if len(request_data) != entry:	# This is not the last entry, so add a comma
					f.write(",\n")
				
			f.write("\n]") # Close the JSON array

		uniqueMacs = set(allMacs)
		print(len(allMacs))
		print(len(uniqueMacs))
		return "OK"

# Driver function 
if __name__ == '__main__': 
    app.run(host='0.0.0.0', port=5000, debug=True, threaded=False)

# Return all scanners that found a specific MAC
def scanners_found_mac(mac):
	scanners = []
	with open(path, "r") as f:
		data = json.load(f)
	for item in data:
		if item["mac"] == mac:
			scanners.append(item["scannerID"])
	return scanners

# Return the distance to a MAC from a specific scanner
def distance_scanner_mac(mac, scanner):
	with open(path, "r") as f:
		data = json.load(f)
	for item in data:
		if item["mac"] == mac & item["scannerID"] == scanner:
			return item["distance"]

def point_angle(pos, L, d_A, d_B, d_C, d_D):
	x, y = pos
	eq_A = (x**2 + (y - L)**2 - d_A**2)**2
	eq_B = ((x - L)**2 + (y -L)**2 - d_B**2)**2
	eq_C = (x**2 + y**2 - d_C**2)**2
	eq_D = ((x - L)**2 + y**2 - d_D**2)**2
	return eq_A + eq_B + eq_C + eq_D

points = []
@app.route('/points', methods = ['GET']) 
def point_translation():
	points = []
	for currentBlock in blocks:
		print("Calculating points for block", currentBlock[0])
		for uniqueMac in uniqueMacs:
			scanners = scanners_found_mac(uniqueMac)
			if scanners in currentBlock[4]:
				d_A = distance_scanner_mac(uniqueMac, currentBlock[4][0])
				d_B = distance_scanner_mac(uniqueMac, currentBlock[4][1])
				d_C = distance_scanner_mac(uniqueMac, currentBlock[4][2])
				d_D = distance_scanner_mac(uniqueMac, currentBlock[4][3])

				L = currentBlock[1]

				# Initial guess
				initial_guess = [L/2, L/2]

				# Minimize the function
				result = minimize(point_angle, initial_guess, args=(L, d_A, d_B, d_C, d_D), method='BFGS')

				# Extract the estimated position
				x, y = result.x
				points.append(result.x)
				