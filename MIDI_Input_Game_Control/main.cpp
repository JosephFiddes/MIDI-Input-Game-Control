/* Converting MIDI action into keyboard press. 

Written by Joseph Fiddes.

Uses the following nonstandard libraries:
RtMidi (unused)
Windows Multimedia API
conio.h (probably unused, it's been a while since I've worked on this)

TO DO:
 - Generalize (currently works specifically with drum input and "Celeste" controls).
 - Check if conio.h is actually used.

*/

#include <iostream>
// #include "RtMidi.h"
#include <Windows.h>
#include <mmeapi.h>
#include <cstdlib>
#include <conio.h>

// constexpr auto SHORT_STRING = 20;
constexpr auto CHR_SPACE = (32);
constexpr auto MIDI_CLOCK = (248);
constexpr auto MIDI_ACTIVE_SENSE = (254);
constexpr auto MIDI_TOTAL_NOTES = (128);
constexpr auto MIDI_TOTAL_CONTROLS = (128);
constexpr auto TOGGLE = (-1);
constexpr auto MAX_KEYS_DOWN = (20);
constexpr auto VELOCITY_THRESHOLD = (8);
constexpr auto CTRL_VELOCITY_THRESHOLD = (100);
constexpr auto CTRL_FOOTPEDAL = (4);

constexpr auto INST_SNARE = (38);
constexpr auto INST_HI_TOM = (48);
constexpr auto INST_MID_TOM = (47);
constexpr auto INST_LO_TOM = (43);
constexpr auto INST_RIDE = (51);
constexpr auto INST_LCRASH = (49);
constexpr auto INST_RCRASH = (57);
constexpr auto INST_BD = (36);

using namespace std;


// FUNCTIONS
void clear_string(char*, int);
int deviceIDof(string);
void CALLBACK MidiInProc(HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
void hold(unsigned int, unsigned int);
void press(unsigned int, unsigned int, DWORD);
void unpress(unsigned int);
void press_key(WORD wVk);
void unpress_key(WORD wVk);
void handleErrors(string, MMRESULT);
bool char_to_vkc(WORD*);
void sub_press_key(WORD wVk, DWORD dwflags);
void my_exit();

// ************************************************************************************************************************************

int MIDI_event_counter;


class note_info {
public:
	WORD key; // The key that is pressed by this note.
	int hold_length; // Held forever if equal to TOGGLE.
	int cancel; // Note cancelled by this note.
	bool depends_on_velocity; 

	void fill(WORD note_key, int note_hold_length, int note_cancels,
		bool note_depends_on_velocity) {
		key = note_key;
		hold_length = note_hold_length;
		cancel = note_cancels;
		depends_on_velocity = note_depends_on_velocity;
	}
};
note_info* note_table;

class control_info {
public:
	WORD key;
};
control_info* control_table;

// A note corresponding to a key being held down.
class key_down {
public:
	int note;
	DWORD up_time;
};

// Class that holds the notes for all the keys currently being held down.
// Doesn't include notes that are being manually held down by player e.g. with a foot pedal.
class keys_down {
public:
	bool bool_array[MIDI_TOTAL_NOTES];
	key_down keys[MAX_KEYS_DOWN];
	int len;

	// Remove the first key from the list.
	key_down pull() {
		int i;

		// Get the return value.
		key_down return_value;
		return_value = keys[0];

		// Remove key from bool_array.
		bool_array[keys[0].note] = false;

		// Move everything down a step.
		for (i = 1; i < len; i++) {
			keys[i - 1].note = keys[i].note;
			keys[i - 1].up_time = keys[i].up_time;
		}
		len--;

		return return_value;
	}

	// Add a key.
	void push(key_down new_key) {
		int i;
		if (bool_array[new_key.note]) {
			// Key already in, let's find it.
			for (i = 0; i < len; i++) {
				if (keys[i].note == new_key.note) {
					keys[i].up_time = new_key.up_time;
					insertion_sort_by_up_time();
					return;
				}
			}

			// It shouldn't be possible to get here.
			cout << "ERROR in class keys_down:" << endl;
			cout << "Key " << new_key.note << " in bool_array, but not in keys." << endl;
			my_exit();
		} 
		else {
			// Key not in, let's add it.
			if (len < MAX_KEYS_DOWN) {
				bool_array[new_key.note] = true;

				keys[len].note = new_key.note;
				keys[len].up_time = new_key.up_time;
				len++;
				insertion_sort_by_up_time();
			}
			else {
				// Can't fit :(
				cout << "WARNING: Too many keys held down." << endl;
				cout << "Input may be missed." << endl;
			}
		}
	}

	// Remove specifically the note given in the function.
	// Returns true if the note is actually in the list.
	// Returns false if the note is not already present.
	bool remove(int note) {
		if (!bool_array[note]) {
			return false;
		}

		// Search for the given note to remove.
		int i;
		for (i = 0; i < len; i++) {
			if (keys[i].note == note) {
				bool_array[note] = false;

				// Move each item forward in the list, overwriting the 
				// given note in the process.
				while (i < len - 1) {
					keys[i].note = keys[i + 1].note;
					keys[i].up_time = keys[i + 1].up_time;

					i++;
				}

				len--;
				return true;
			}
		}
		
		// It should be impossible to get here.
		cout << "ERROR in class keys_down:" << endl;
		cout << "Key " << note << " in bool_array, but not in keys." << endl;
		my_exit();

	}

private:
	void insertion_sort_by_up_time() {
		/* Algorithm provided by
		https://www.geeksforgeeks.org/insertion-sort/
		*/
		int i, j;
		key_down pivot_key;

		for (i = 1; i < len; i++) {
			pivot_key = keys[i];
			j = i - 1;

			while (j >= 0 && keys[j].up_time > pivot_key.up_time) {
				keys[j + 1].note = keys[j].note;
				keys[j + 1].up_time = keys[j].up_time;
				j--;
			}
			keys[j + 1].note = pivot_key.note;
			keys[j + 1].up_time = pivot_key.up_time;
		}
	}
};

keys_down initialize_keys_down();

keys_down keys_dn;

// ************************************************************************************

int main()
{
	note_table = new note_info[MIDI_TOTAL_NOTES];
	control_table = new control_info[MIDI_TOTAL_CONTROLS];
	keys_dn = initialize_keys_down();

	// Populate note_table
	note_table[INST_SNARE].fill(VK_LEFT, 300, INST_LO_TOM, false); // Snare

	note_table[INST_HI_TOM].fill(VK_UP, 300, INST_MID_TOM, false); 
	note_table[INST_MID_TOM].fill(VK_DOWN, 300, INST_HI_TOM, false); // Toms
	note_table[INST_LO_TOM].fill(VK_RIGHT, 300, INST_SNARE, false);

	note_table[INST_RIDE].fill('x', 300, NULL, false); // Ride
	note_table[INST_LCRASH].fill(VK_ESCAPE, 300, NULL, false); // LCrash
	note_table[INST_RCRASH].fill(VK_TAB, 300, NULL, false); // RCrash

	note_table[INST_BD].fill('c', 3, NULL, true); // BD

	// Populate control_table
	control_table[CTRL_FOOTPEDAL].key = 'z';
	
	/*int i;
	for (i = 0; i < MIDI_TOTAL_NOTES; i++) {
		cout << "note_table[" << i << "]" << endl;
		cout << (note_table[i].key) << ", ";
		cout << note_table[i].hold_length << ", ";
		cout << note_table[i].cancels << ", ";
		cout << note_table[i].total_cancels << ", ";
		cout << note_table[i].depends_on_velocity << ", ";
		cout << endl << endl;
	}*/

	// Get the state of the keyboard.
	BYTE lpKeyState[256];
	if (GetKeyboardState(lpKeyState)) {
		int i;
		for (i = 0; i < 256; i++) {
			/*cout << (int)lpKeyState[i] << " ";
			if (!((i+1) % 15)) {
				cout << endl;
			}*/

			if ((lpKeyState[i] & (BYTE)(-1)) == (BYTE)(-1)) {
				// Key is on.
				unpress_key(lpKeyState[i]);
			}
		}
	}
	else {
		cout << "oh no" << endl;
		my_exit();
	}

	// void (*MidiProcessing)(HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) = MidiInProc;
	MMRESULT error_result;

	// Find the ID of the midi input device.
	int device_ID = deviceIDof("DTX drums");
	
	HMIDIIN* device_handle = new HMIDIIN; 
	DWORD* dwInstance = new DWORD;

	// Open MIDI device.
	error_result = midiInOpen(device_handle, device_ID, reinterpret_cast<DWORD_PTR>(MidiInProc), (DWORD_PTR)dwInstance, CALLBACK_FUNCTION);
	handleErrors("midiInOpen", error_result);

	// Start MIDI device.
	error_result = midiInStart(*device_handle);
	handleErrors("midiInStart", error_result);
	
	// cout << device_handle << endl;

	// Idle here until SPACE is pressed.
	// It is during this loop that MIDI input is processed.
	// Curiously, the function that processes MIDI is not called within this loop,
	// but is actually called using a different thing.
	cout << "Ready for MIDI input." << endl;
	while (true) {
		if (_kbhit()) {
			if (_getch() == CHR_SPACE) {
				break;
			}
		}
	}

	// Stop MIDI device.
	error_result = midiInStop(*device_handle);
	handleErrors("midiInStop", error_result);

	// Close MIDI device
	error_result = midiInClose(*device_handle);
	handleErrors("midiInClose", error_result);

	cout << "MIDI events: " << MIDI_event_counter << endl;

	cout << "fin" << endl;
	return 0;
}

// Find the ID of the device that we're looking for.
int deviceIDof(string device_name) {
	// RtMIDI stuff
	/*
	try {
		RtMidiIn midiin;
	}
	catch (RtMidiError &error) {
		// Handle exeption
		error.printMessage();
	}
	*/

	cout << "Searching for " << device_name << "..." << endl;

	int i, j;
	int total_devices = midiInGetNumDevs();

	
	if (!total_devices) {
		cout << "No MIDI devices found." << endl;
		my_exit();
	}

	cout << "Total devices: " << total_devices << endl;

	MIDIINCAPS* device_info = new MIDIINCAPS[1];
	MMRESULT err_result;

	// Get the name of each device, and check if it's the requested device.
	for (i = 0; i < total_devices; i++) {
		err_result = midiInGetDevCaps(i, device_info, sizeof(MIDIINCAPS));

		// Error handling.
		handleErrors("midiInGetDevCaps", err_result);

		char c;
		char* potential_device_name = new char[MAXPNAMELEN];

		clear_string(potential_device_name, MAXPNAMELEN);

		// Get device name.
		j = 0;
		while ((c = (char)(device_info->szPname)[j]) != '\0') {
			potential_device_name[j] = c;
			j++;
		}

		cout << potential_device_name << endl;

		// Have we found the correct device?
		if (!device_name.compare(potential_device_name)) {
			cout << "Found " << device_name << "!" << endl;
			return i;
		}
	}

	// If this section is reached, then the requested device is not found.
	cout << device_name << " not found." << endl;
	my_exit();
}

// Replaces all characters in a string with '\0'.
void clear_string(char* str, int length) {
	int i;
	for (i = 0; i < length; i++) {
		str[i] = '\0';
	}
}

// Processes MIDI information (?)
void CALLBACK MidiInProc(HMIDIIN device_handle, UINT message, DWORD_PTR dwInstance , DWORD_PTR parameter1, DWORD_PTR parameter2) {
	unsigned int midi_status, midi_data1, midi_data2, note, velocity;
	DWORD delta_time;

	delta_time = parameter2;

	// Unpress buttons which are due for an unpress.
	while ((keys_dn.len) && (delta_time > keys_dn.keys[0].up_time)) {
		unpress(keys_dn.keys[0].note);
	}

	switch (message) {
	case MIM_DATA: 
		midi_status = parameter1 & 0xff;
		if (midi_status != MIDI_CLOCK && midi_status != MIDI_ACTIVE_SENSE) {
			/*cout << "MIDI Message: Data" << endl;
			cout << "MIDI Status: " << midi_status << endl;*/
			
			midi_data1 = (parameter1 >> 8) & 0xff;
			midi_data2 = (parameter1 >> 16) & 0xff;

			// Note down
			if (144 <= midi_status && midi_status <= 159) {
				note = midi_data1;
				velocity = midi_data2;

				if ((1 <= velocity) && (velocity < VELOCITY_THRESHOLD)) {
					cout << "(Too quiet to be interpretted as an input.)" << endl;
				}
				else {
					// cout << "(" << note << ", " << velocity << ")" << endl;

					// NoteOn (as opposed to NoteOff)
					if (velocity) {
						press(note, velocity, delta_time);
					}
				}
			}
			// Control / Mode Change
			else if (176 <= midi_status && midi_status <= 191) {
				velocity = midi_data2;

				if (midi_data1 == CTRL_FOOTPEDAL) {
					// Foot pedal being held or unheld.
					hold(midi_data1, velocity);
				}
				else {
					// We don't know what's happening with input.
					cout << "WARNING: Control mode " << midi_data1 << " unrecognised!" << endl;
				}
			}

			// When referring to previous code (in python):
			// midi_events[i][0][1] refers to "note"
			// midi_events[i][0][2] refers to "velocity"

			// cout << "data 1: " << midi_data1 << endl;
			// cout << "data 2: " << midi_data2 << endl << endl;
			// cout << "status: " << midi_status << endl;
		}
		MIDI_event_counter++;
		break;
	case MIM_LONGDATA:
		cout << "MIDI Message: Long Data" << endl;
		my_exit();
		break;
	case MIM_MOREDATA:
		cout << "MIDI Message: More Data" << endl;
		my_exit();
		break;
	case MIM_OPEN:
		cout << "MIDI Message: Open" << endl;
		break;
	case MIM_CLOSE:
		cout << "MIDI Message: Close" << endl;
		break;
	case MIM_ERROR:
		cout << "MIDI Message: Error" << endl;
		my_exit();
		break;
	case MIM_LONGERROR:
		cout << "MIDI Message: Long Error" << endl;
		my_exit();
		break;
	default:
		cout << "MIDI Message: Default (should not be possible)" << endl;
		my_exit();
		break;
	}
}

// Holds a button associated with a control.
void hold(unsigned int control, unsigned int velocity) {
	if (velocity >= CTRL_VELOCITY_THRESHOLD) {
		press_key(control_table[control].key);
	}
	else {
		unpress_key(control_table[control].key);
	}
}

// Presses keyboard button associated with note input.
void press(unsigned int note, unsigned int velocity, DWORD delta_time) {
	key_down new_key_down;
	int cancel;
	DWORD up_time;

	if (note_table[note].depends_on_velocity) {
		up_time = delta_time + velocity * note_table[note].hold_length;
	}
	else {
		up_time = delta_time + note_table[note].hold_length;
	}
	
	new_key_down.note = note;
	new_key_down.up_time = up_time;

	// Unpress any conflicting buttons.
	if (cancel = note_table[note].cancel) {
		if (keys_dn.remove(cancel)) {
			unpress_key(note_table[cancel].key);
		}
	}

	press_key(note_table[note].key);

	keys_dn.push(new_key_down);
}

// Unpresses keyboard button associated with note input.
void unpress(unsigned int note) {
	unpress_key(note_table[note].key);

	keys_dn.pull();
}

// Presses keyboard key.
void press_key(WORD wVk) {
	sub_press_key(wVk, 0);
}

// Unpresses keyboard key.
void unpress_key(WORD wVk) {
	sub_press_key(wVk, KEYEVENTF_KEYUP);
}

// Subfunction of press_key() and unpress_key().
void sub_press_key(WORD wVk, DWORD dwflags) {
	/*if (dwflags == KEYEVENTF_KEYUP) {
		cout << "Releasing " << wVk << endl;
	}
	else {
		cout << "Pressing " << wVk << endl;
	}
	*/

	INPUT* inputs = new INPUT;

	// Convert ASCII character to Virtual keycode.
	if (char_to_vkc(&wVk)) {
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = wVk;
		inputs[0].ki.wScan = 0x0;
		inputs[0].ki.dwFlags = dwflags;
		inputs[0].ki.time = 0;

		// Sends a keyboard input.
		SendInput(1, inputs, sizeof(INPUT));
	}
}

// Converts ASCII character to Virtual keycode.
bool char_to_vkc(WORD* c) {
	if ((VK_LEFT <= *c && *c <= VK_DOWN) || ('0' <= *c && *c <= '9') 
		|| *c == VK_ESCAPE || *c == VK_TAB) {
		// Do nothing, it's already right.
	}
	else if ('a' <= *c && *c <= 'z') {
		*c -= 0x20;
	}
	else {
		cout << "Character not within given constraints." << endl;
		return false;
	}

	return true;
}

// Create a new keys_down.
keys_down initialize_keys_down() {
	keys_down new_keys_down;
	int i;

	// Make sure every value in the keys_down array is false.
	for (i = 0; i < MIDI_TOTAL_NOTES; i++) {
		new_keys_down.bool_array[i] = false;
	}

	new_keys_down.len = 0;

	return new_keys_down;
}

// Handles errors for Windows MIDI stuff
void handleErrors(string func_name, MMRESULT error_result) {
	switch (error_result) {
	case MMSYSERR_NOERROR:
		cout << "All good in " << func_name << "." << endl;
		break;
	case MMSYSERR_NODRIVER:
		cout << "Error in " << func_name << ": The driver is not installed" << endl;
		my_exit();
		break;
	case MMSYSERR_ALLOCATED:
		cout << "Error in " << func_name << ": The specified resource is already allocated." << endl;
		my_exit();
		break;
	case MMSYSERR_BADDEVICEID:
		cout << "Error in " << func_name << ": The specified device identifier is out of range." << endl;
		my_exit();
		break;
	case MMSYSERR_INVALFLAG:
		cout << "Error in " << func_name << ": The flags specified by dwFlags are invalid." << endl;
		my_exit();
		break;
	case MMSYSERR_INVALPARAM:
		cout << "Error in " << func_name << ": The specified pointer or structure is invalid." << endl;
		my_exit();
		break;
	case MMSYSERR_NOMEM:
		cout << "Error in " << func_name << ": The system is unable to allocate or lock memory." << endl;
		my_exit();
		break;
	case MIDIERR_STILLPLAYING:
		cout << "Error in " << func_name << ": Buffers are still in the queue." << endl;
		my_exit();
	case MMSYSERR_INVALHANDLE:
		cout << "Error in " << func_name << ": The specified device handle is invalid." << endl;
		my_exit();
	default:
		// It should be impossible to get here.
		cout << "Error in " << func_name << ": Somehow gotten error value not specified in documentation." << endl;
		my_exit();
		break;
	}
}

// Exits
void my_exit() {
	cout << endl << "EXIT" << endl;
	::exit(EXIT_FAILURE);
}