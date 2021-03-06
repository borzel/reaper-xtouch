/*
** reaper_csurf
** Copyright (C) 2006-2008 Cockos Incorporated
** License: LGPL.
*/

#include "csurf.h"
#include <string>
using namespace std;

extern reaper_csurf_reg_t csurf_APCKeys25_reg;

REAPER_PLUGIN_HINSTANCE g_hInst; // used for dialogs, if any

extern "C"
{

	REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec)
	{
		if (rec)
		{
			REAPERAPI_LoadAPI(rec->GetFunc);

			g_hInst = hInstance;

			rec->Register("csurf", &csurf_APCKeys25_reg);
		}

		return 1;

	}
};





#ifndef _WIN32 // MAC resources
#include "../../WDL/swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#undef BEGIN
#undef END
#include "../../WDL/swell/swell-menugen.h"
#include "res.rc_mac_menu"
#endif


#ifndef _WIN32 // let OS X use this threading step

#include "../../WDL/mutex.h"
#include "../../WDL/ptrlist.h"



class threadedMIDIOutput : public midi_Output
{
public:
	threadedMIDIOutput(midi_Output *out)
	{
		m_output = out;
		m_quit = false;
		DWORD id;
		m_hThread = CreateThread(NULL, 0, threadProc, this, 0, &id);
	}
	virtual ~threadedMIDIOutput()
	{
		if (m_hThread)
		{
			m_quit = true;
			WaitForSingleObject(m_hThread, INFINITE);
			CloseHandle(m_hThread);
			m_hThread = 0;
			Sleep(30);
		}

		delete m_output;
		m_empty.Empty(true);
		m_full.Empty(true);
	}

	virtual void SendMsg(MIDI_event_t *msg, int frame_offset) // frame_offset can be <0 for "instant" if supported
	{
		if (!msg) return;

		WDL_HeapBuf *b = NULL;
		if (m_empty.GetSize())
		{
			m_mutex.Enter();
			b = m_empty.Get(m_empty.GetSize() - 1);
			m_empty.Delete(m_empty.GetSize() - 1);
			m_mutex.Leave();
		}
		if (!b && m_empty.GetSize() + m_full.GetSize() < 500)
			b = new WDL_HeapBuf(256);

		if (b)
		{
			int sz = msg->size;
			if (sz < 3)sz = 3;
			int len = msg->midi_message + sz - (unsigned char *)msg;
			memcpy(b->Resize(len, false), msg, len);
			m_mutex.Enter();
			m_full.Add(b);
			m_mutex.Leave();
		}
	}

	virtual void Send(unsigned char status, unsigned char d1, unsigned char d2, int frame_offset) // frame_offset can be <0 for "instant" if supported
	{
		MIDI_event_t evt = { 0,3,status,d1,d2 };
		SendMsg(&evt, frame_offset);
	}

	///////////

	static DWORD WINAPI threadProc(LPVOID p)
	{
		WDL_HeapBuf *lastbuf = NULL;
		threadedMIDIOutput *_this = (threadedMIDIOutput*)p;
		unsigned int scnt = 0;
		for (;;)
		{
			if (_this->m_full.GetSize() || lastbuf)
			{
				_this->m_mutex.Enter();
				if (lastbuf) _this->m_empty.Add(lastbuf);
				lastbuf = _this->m_full.Get(0);
				_this->m_full.Delete(0);
				_this->m_mutex.Leave();

				if (lastbuf) _this->m_output->SendMsg((MIDI_event_t*)lastbuf->Get(), -1);
				scnt = 0;
			}
			else
			{
				Sleep(1);
				if (_this->m_quit&&scnt++ > 3) break; //only quit once all messages have been sent
			}
		}
		delete lastbuf;
		return 0;
	}

	WDL_Mutex m_mutex;
	WDL_PtrList<WDL_HeapBuf> m_full, m_empty;

	HANDLE m_hThread;
	bool m_quit;
	midi_Output *m_output;
};




midi_Output *CreateThreadedMIDIOutput(midi_Output *output)
{
	if (!output) return output;
	return new threadedMIDIOutput(output);
}

#else

// windows doesnt need it since we have threaded midi outputs now
midi_Output *CreateThreadedMIDIOutput(midi_Output *output)
{
	return output;
}

#endif

static bool g_csurf_mcpmode = true; // we may wish to allow an action to set this

static double charToVol(unsigned char val)
{
	double pos = ((double)val*1000.0) / 127.0;
	pos = SLIDER2DB(pos);
	return DB2VAL(pos);

}

static  unsigned char volToChar(double vol, const double max)
{
	double d = (DB2SLIDER(VAL2DB(vol))*max / 1000.0);
	if (d < 0.0)d = 0.0;
	else if (d > max)d = max;

	return (unsigned char)(d + 0.5);
}

static double charToPan(unsigned char val)
{
	double pos = ((double)val*1000.0 + 0.5) / 127.0;

	pos = (pos - 500.0) / 500.0;
	if (fabs(pos) < 0.08) pos = 0.0;

	return pos;
}

static unsigned char panToChar(double pan)
{
	pan = (pan + 1.0)*63.5;

	if (pan < 0.0)pan = 0.0;
	else if (pan > 127.0)pan = 127.0;

	return (unsigned char)(pan + 0.5);
}


class TrackIterator {
	int m_index;
	int m_len;
public:
	TrackIterator() {
		m_index = 1;
		m_len = CSurf_NumTracks(false);
	}
	MediaTrack* operator*() {
		return CSurf_TrackFromID(m_index, false);
	}
	TrackIterator &operator++() {
		if (m_index <= m_len) ++m_index;
		return *this;
	}
	bool end() {
		return m_index > m_len;
	}
};

class MIDI_Message
{
public:
	MIDI_Message() {
		evt.frame_offset = 0;
		evt.size = 0;
		memset(data, 0, 512);
	}
	MIDI_event_t evt;
	char data[512];
};

/*

MIDI Konstanten

*/
const int NOTE_ON = 0x90;
const int CC = 0xb0;
const int SYSEX = 0xf0;
const int CHANNEL_MASK = 0x0f;
const int PITCH_BEND = 0xe0;


#define FIXID(id) int id=CSurf_TrackToID(trackid,g_csurf_mcpmode); int oid=id; id -= m_offset;

class CSurf_APCKeys25 : public IReaperControlSurface
{
	int m_midi_in_dev, m_midi_out_dev;
	int m_offset, m_size, bank;
	midi_Output *m_midiout;
	midi_Input *m_midiin;

	WDL_String descspace;
	char configtmp[1024];

	int m_num_tracks = 0;

	bool m_isPlaying = false;

	unsigned int m_pan_lasttouch[256];
	unsigned int m_vol_lasttouch[256];

	int m_vol_lastpos[256];
	int m_pan_lastpos[256];
	bool m_mute_laststate[256];
	bool m_solo_laststate[256];

	int m_send1_vol_lastpos[256];
	int m_send2_vol_lastpos[256];
	int m_send3_vol_lastpos[256];

	int m_vol_levelmeter_lastpos[256];
	DWORD last_levelmeterupdate;

	string m_displaytext;
	string m_displaytext2;
	bool m_update_display_next_cycle = false;
	DWORD last_levelmetertextupdate;



public:
	CSurf_APCKeys25(int offset, int size, int indev, int outdev, int *errStats)
	{
		m_offset = offset;
		m_size = size;
		bank = 0;
		m_midi_in_dev = indev;
		m_midi_out_dev = outdev;

		memset(m_vol_lastpos, 0xff, sizeof(m_vol_lastpos));
		memset(m_pan_lastpos, 0xff, sizeof(m_pan_lastpos));
		memset(m_mute_laststate, 0, sizeof(m_mute_laststate));
		memset(m_solo_laststate, 0, sizeof(m_solo_laststate));

		memset(m_pan_lasttouch, 0, sizeof(m_pan_lasttouch));
		memset(m_vol_lasttouch, 0, sizeof(m_vol_lasttouch));

		memset(m_send1_vol_lastpos, 0, sizeof(m_send1_vol_lastpos));
		memset(m_send2_vol_lastpos, 0, sizeof(m_send2_vol_lastpos));
		memset(m_send3_vol_lastpos, 0, sizeof(m_send3_vol_lastpos));

		memset(m_vol_levelmeter_lastpos, 0, sizeof(m_vol_levelmeter_lastpos));
		//memset(levelmeter_stoptime, 0, sizeof(levelmeter_stoptime));
		last_levelmeterupdate = 0;
		last_levelmetertextupdate = 0;

		m_displaytext = string("");
		m_displaytext2 = string("");

		//create midi hardware access
		m_midiin = m_midi_in_dev >= 0 ? CreateMIDIInput(m_midi_in_dev) : NULL;
		m_midiout = m_midi_out_dev >= 0 ? CreateThreadedMIDIOutput(CreateMIDIOutput(m_midi_out_dev, false, NULL)) : NULL;

		if (errStats)
		{
			if (m_midi_in_dev >= 0 && !m_midiin) *errStats |= 1;
			if (m_midi_out_dev >= 0 && !m_midiout) *errStats |= 2;
		}

		if (m_midiin)
			m_midiin->start();

	}
	~CSurf_APCKeys25()
	{
		delete m_midiout;
		delete m_midiin;
	}


	const char *GetTypeString() { return "X-Touch"; }
	const char *GetDescString()
	{
		descspace.Set("X-Touch");
		char tmp[512];
		sprintf(tmp, " (dev %d,%d)", m_midi_in_dev, m_midi_out_dev);
		descspace.Append(tmp);
		return descspace.Get();
	}
	const char *GetConfigString() // string of configuration data
	{
		sprintf(configtmp, "%d %d %d %d", m_offset, m_size, m_midi_in_dev, m_midi_out_dev);
		return configtmp;
	}

	void CloseNoReset()
	{
		delete m_midiout;
		delete m_midiin;
		m_midiout = 0;
		m_midiin = 0;
	}



	bool SomethingSoloed() {
		for (TrackIterator ti; !ti.end(); ++ti) {
			MediaTrack *tr = *ti;
			int* OriginalState = (int*)GetSetMediaTrackInfo(tr, "I_SOLO", NULL);
			if (*OriginalState > 0)
				return true;
		}

		return false;
	}

	void DebugMidi(MIDI_event_t *evt)
	{
		// Dezimale Debugausgabe
		OutputDebugString("Midi-Message: ");
		for (int x = 0; x < evt->size; x++)
		{
			OutputDebugStringA(to_string(evt->midi_message[x]).c_str());
			OutputDebugString(" ");
		}
		OutputDebugString("  Hex: ");
		for (int x = 0; x < evt->size; x++)
		{
			char text[3];
			sprintf(text, "%02X", evt->midi_message[x]);
			OutputDebugString(text);
			OutputDebugString(" ");
		}
		OutputDebugString("\n");
	}

	int int2digit(int value)
	{
		switch (value)
		{
		case 0:
			return 63;
		case 1:
			return 6;
		case 2:
			return 91;
		case 3:
			return 79;
		default:
			return 0;
		}
	}

	void Run()
	{

		DWORD now = timeGetTime();

		// Levelmeteranzeige
		if (now > (last_levelmeterupdate + 50)) // Update erst nach X ms wieder
		{
			if (GetPlayState() & 1)
			{
				last_levelmeterupdate = now;

				//bool somethingSoloed = SomethingSoloed();
				/*string display_levelmeter_text = "";
				string levelmeter_text = "";*/

				for (int x = 0; x <= 7; x++)
				{
					MediaTrack *tr = CSurf_TrackFromID(x + bank * 8 + m_offset, g_csurf_mcpmode);

					if (tr != NULL)
					{
						int level = 0;
						double pp = (Track_GetPeakInfo(tr, 0) + Track_GetPeakInfo(tr, 1)) * 0.5;

						level = volToChar(pp, 9);
						//int levelmeter = (level >> 4) & 0x0f;
						//int levelmeter = 7;

						/*if (level <= 10)       levelmeter = 0;
						else if (level > 10 & level <= 20)  levelmeter = 1;
						else if (level > 20 & level <= 30)  levelmeter = 2;
						else if (level > 30 & level <= 40)  levelmeter = 3;
						else if (level > 40 & level <= 50)  levelmeter = 4;
						else if (level > 50) levelmeter = 5;
						else if (level > 60) levelmeter = 6;*/

						if (m_vol_levelmeter_lastpos[x] != level)
						{
							m_vol_levelmeter_lastpos[x] = level;

							int midivalue = level + (x * 16);
							m_midiout->Send(0xD0, midivalue, 0, -1);
							char text[20];
							sprintf(text, "Vol: %d\n", level);
							OutputDebugString(text);
						}

						//display_levelmeter_text.append(levelmeter_text);
					}

				}

				/*if (m_displaytext != display_levelmeter_text)
				{
					m_displaytext.assign(display_levelmeter_text);
					m_update_display_next_cycle = true;
				}*/

			}
		}

		if (m_midiin)
		{
			m_midiin->SwapBufs(timeGetTime());
			int l = 0;
			MIDI_eventlist *list = m_midiin->GetReadBuf();
			MIDI_event_t *evts;
			while ((evts = list->EnumItems(&l))) OnMIDIEvent(evts);
		}
	}


	void OnMIDIEvent(MIDI_event_t *evt)
	{
		// X-Touch SYS-Ex Keepalive-Message
		if (evt->midi_message[0] == 0xf0 &&
			evt->midi_message[1] == 0x00 &&
			evt->midi_message[2] == 0x20 &&
			evt->midi_message[3] == 0x32 &&
			evt->midi_message[4] == 0x58 &&
			evt->midi_message[5] == 0x54 &&
			evt->midi_message[6] == 0x00 &&
			evt->midi_message[7] == 0xf7)
		{
			//OutputDebugString("KeepAlive empfangen... ");

			// Antwort zurücksenden, damit Verbindung aktiv bliebt

			//Sysex bauen
			MIDI_Message mm;

			//Sysex Header
			mm.evt.midi_message[mm.evt.size++] = 0xF0;

			// X-­‐Touch KeepAlive-Antwort: 00 00 66 14 00
			mm.evt.midi_message[mm.evt.size++] = 0x00;
			mm.evt.midi_message[mm.evt.size++] = 0x00;
			mm.evt.midi_message[mm.evt.size++] = 0x66;
			mm.evt.midi_message[mm.evt.size++] = 0x14;
			mm.evt.midi_message[mm.evt.size++] = 0x00;

			// Abschluß
			mm.evt.midi_message[mm.evt.size++] = 0xF7;

			if (m_midiout) {
				m_midiout->SendMsg(&mm.evt, -1);
				//OutputDebugString("KeepAlive gesendet\n");
			}

		}

		else if (evt->size >= 3)
		{



			// Eventtyp feststellen
			int midiStatusTyp = evt->midi_message[0] & 0xF0;
			bool midi_NoteON = midiStatusTyp == NOTE_ON;
			bool midi_CC = midiStatusTyp == CC;
			bool sysex = midiStatusTyp == SYSEX;
			bool pitchbend = midiStatusTyp == PITCH_BEND;
			int midi_Channel = evt->midi_message[0] & CHANNEL_MASK;

			/*if (sysex)
			{
				OutputDebugString("SYSEX: ");
				for (int x = 0; x < evt->size; x++)
				{
					char text[3];
					sprintf(text, "%02X", evt->midi_message[x]);
					OutputDebugString(text);
					OutputDebugString(" ");
				}
				OutputDebugString("\n");

			}*/

			if (pitchbend)
			{
				int lsb = evt->midi_message[1] & 0x7f;
				int msb = evt->midi_message[2] & 0x7f;
				long pitchbendvalue = (msb << 7) + lsb;

				MediaTrack *tr;
				if (midi_Channel == 8)
				{
					// Mastertrack
					tr = CSurf_TrackFromID(0, g_csurf_mcpmode);
				}
				else
				{
					// Alle anderen
					tr = CSurf_TrackFromID(midi_Channel + bank * 8 + m_offset, g_csurf_mcpmode);
				}
				if (tr)
				{
					CSurf_SetSurfaceVolume(tr, CSurf_OnVolumeChange(tr, charToVol(pitchbendvalue >> 7), false), this);
				}


				OutputDebugString("Pitchbend Channel: ");
				OutputDebugString(to_string(midi_Channel).c_str());
				OutputDebugString(" MSB: ");
				OutputDebugString(to_string(msb).c_str());
				OutputDebugString(" LSB: ");
				OutputDebugString(to_string(lsb).c_str());
				OutputDebugString(" Value: ");
				OutputDebugString(to_string(pitchbendvalue).c_str());
				OutputDebugString("\n");


			}


			if (midi_NoteON)
			{

				int midi_Note = evt->midi_message[1] & 0x7F; // nur die 7 ersten Bits auswerten
				int midi_Velocity = evt->midi_message[2] & 0x7F; // nur die 7 ersten Bits auswerten


				if (midi_Velocity == 127 && midi_Note >= 8 && midi_Note <= 15)
				{
					/*
					SOLO
					*/

					int trackid = (midi_Note - 8) + bank * 8 + m_offset;
					MediaTrack *tr = CSurf_TrackFromID(trackid, g_csurf_mcpmode);

					// Zustand umkehren
					bool solo = !m_solo_laststate[trackid];

					if (tr)
					{
						OutputDebugString("Solo: Track: ");
						OutputDebugString(to_string(trackid).c_str());
						OutputDebugString("\n");
						CSurf_SetSurfaceSolo(tr, CSurf_OnSoloChange(tr, solo), this);
						// Zustand merken
						m_solo_laststate[trackid] = solo;
					}
				}
				else if (midi_Velocity == 127 && midi_Note >= 16 && midi_Note <= 23)
				{
					/*
					MUTE
					*/

					int trackid = (midi_Note - 16) + bank * 8 + m_offset;
					MediaTrack *tr = CSurf_TrackFromID(trackid, g_csurf_mcpmode);

					// Zustand umkehren
					bool mute = !m_mute_laststate[trackid];

					if (tr)
					{
						OutputDebugString("Mute: Track: ");
						OutputDebugString(to_string(trackid).c_str());
						OutputDebugString("\n");
						CSurf_SetSurfaceMute(tr, CSurf_OnMuteChange(tr, mute), this);
						// Zustand merken
						m_mute_laststate[trackid] = mute;
					}
				}
				else if (midi_Velocity == 127 && midi_Note >= 24 && midi_Note <= 31)
				{
					/*
					SELECT
					*/

					int trackid = (midi_Note - 24) + bank * 8 + m_offset;
					MediaTrack *tr = CSurf_TrackFromID(trackid, g_csurf_mcpmode);

					if (tr)
					{
						OutputDebugString("Select: Track: ");
						OutputDebugString(to_string(trackid).c_str());
						OutputDebugString("\n");
						CSurf_OnTrackSelection(tr);
						SetOnlyTrackSelected(tr);

					}

				}
				else if (midi_Velocity == 127 && midi_Note == 46)
				{
					/*
					FADER BANK <-
					*/

					if (bank > 0)
					{
						AdjustBankShift(false);
					}
				}
				else if (midi_Velocity == 127 && midi_Note == 47)
				{
					/*
					FADER BANK <-
					*/

					if (bank < 256) {
						AdjustBankShift(true);
					}
				}
				else if (midi_Velocity == 127 && midi_Note == 91)
				{
					/*
					Start
					*/

					CSurf_GoStart();
				}
				else if (midi_Velocity == 127 && midi_Note == 92)
				{
					/*
					Ende
					*/

					CSurf_GoEnd();
				}
				else if (midi_Velocity == 127 && midi_Note == 93)
				{
					/*
					STOP
					*/

					CSurf_OnStop();
				}
				else if (midi_Velocity == 127 && midi_Note == 94)
				{
					/*
					PLAY / PAUSE
					*/

					if (GetPlayState())
						CSurf_OnPause();
					else
						CSurf_OnPlay();
				}
				else if (midi_Velocity == 127 && midi_Note == 95)
				{
					/*
					RECORD
					*/

					CSurf_OnRecord();
				}

				else
				{
					DebugMidi(evt);
				}


			}
			else
			{
				//DebugMidi(evt);
			}





			//	else if (evt->midi_message[1] >= 0x21 && evt->midi_message[1] <= 0x28) // pan reset
			//	{
			//		int trackid = ((evt->midi_message[1] - 0x21) & 7) + bank * 8 + m_offset;
			//		m_pan_lasttouch[trackid & 0xff] = GetTickCount();
			//		MediaTrack *tr = CSurf_TrackFromID(trackid, g_csurf_mcpmode);
			//		if (tr) CSurf_SetSurfacePan(tr, CSurf_OnPanChange(tr, 0.0, false), NULL);
			//	}
			//	else if (evt->midi_message[1] >= 0x01 && evt->midi_message[1] <= 0x08) // pan set
			//	{
			//		int trackid = (evt->midi_message[1] - 0x01) + bank * 8 + m_offset;
			//		m_pan_lasttouch[trackid & 0xff] = GetTickCount();
			//		m_pan_lastpos[trackid & 0xff] = evt->midi_message[2];

			//		MediaTrack *tr = CSurf_TrackFromID(trackid, g_csurf_mcpmode);
			//		if (tr) CSurf_SetSurfacePan(tr, CSurf_OnPanChange(tr, charToPan(evt->midi_message[2]), false), this);
			//	}

		}
	}




	void SetTrackListChange()
	{
		int firstTrackId = bank * 8 + m_offset;
		for (int idx = firstTrackId, ctrack = 0; idx <= firstTrackId + 7; idx++)
		{
			MediaTrack *tr = CSurf_TrackFromID(idx, g_csurf_mcpmode);

			string title;
			bool empty = false;
			if (tr)
			{
				// Name
				title = string((char*)GetSetMediaTrackInfo(tr, "P_NAME", 0));
			}
			else
			{
				title = string("");
				empty = true;
			}

			const int maxlenght = 7;
			if (title.length() > maxlenght)
			{
				title.erase(maxlenght);
			}
			else if (title.length() < maxlenght)
			{
				title.append(maxlenght - title.length(), 0);
			}

			// SYSEX für LCD bauen
			MIDI_Message mm;

			//Sysex Header
			mm.evt.midi_message[mm.evt.size++] = 0xF0;

			// X-Touch LCD-Header
			mm.evt.midi_message[mm.evt.size++] = 0x00;
			mm.evt.midi_message[mm.evt.size++] = 0x00;
			mm.evt.midi_message[mm.evt.size++] = 0x66;
			mm.evt.midi_message[mm.evt.size++] = 0x58;

			// Channel (0x20-0x27)
			mm.evt.midi_message[mm.evt.size++] = 0x20 + ctrack;

			// Color (0x01-0x07 und 0x41-0x47)
			mm.evt.midi_message[mm.evt.size++] = 0x47; // weiß mit heller 2. Zeile

			// Content Line 1 (Channelnummer) [ Ch xx ]
			char line1[7] = { 0,0,0,0,0,0,0 };
			if (!empty) sprintf(line1, "CH %d", idx);
			for (int x = 0; x < 7; x++)
			{
				mm.evt.midi_message[mm.evt.size++] = line1[x];
			}

			// Content Line 2
			for (int x = 0; x < 7; x++)
			{
				if (title[x] >= 0 && title[x] <= 127)
					mm.evt.midi_message[mm.evt.size++] = title[x];
				else
					mm.evt.midi_message[mm.evt.size++] = '?';
			}

			// Abschluß
			mm.evt.midi_message[mm.evt.size++] = 0xF7;

			if (m_midiout) {
				m_midiout->SendMsg(&mm.evt, -1);
				//DebugMidi(&mm.evt);
			}
			//}



			ctrack++;
		}
	}


	void SetSurfaceMute(MediaTrack *trackid, bool mute)
	{
		FIXID(id)
			if (m_midiout && id >= 0 && id < 256 && id < m_size)
			{
				m_mute_laststate[oid] = mute;

				if (IsInCurrentControlView(id))
				{
					SendMidiMute(id, mute);
				}
			}
	}
	void SendMidiMute(int id, bool mute)
	{
		m_midiout->Send(NOTE_ON, 16 + (id % 8), mute ? 2 : 0, -1);
	}

	void SetSurfaceSolo(MediaTrack *trackid, bool solo)
	{
		FIXID(id)


			if (m_midiout && id >= 0 && id < 256 && id < m_size)
			{
				m_solo_laststate[oid] = solo;

				if (IsInCurrentControlView(id))
				{
					SendMidiSolo(id, solo);
				}
			}
	}
	void SendMidiSolo(int id, bool solo)
	{
		m_midiout->Send(NOTE_ON, 8 + (id % 8), solo ? 2 : 0, -1);
	}


	void SetSurfaceVolume(MediaTrack *trackid, double volume)
	{
		FIXID(id)
			if (m_midiout && id >= 0 && id < 256 && id < m_size)
			{
				unsigned char volch = volToChar(volume, 127);

				if (m_vol_lastpos[oid] != volch)
				{
					m_vol_lastpos[oid] = volch;

					if (IsInCurrentControlView(id))
					{
						SendMidiVolume(id, volch);
					}
				}
			}
	}
	void SendMidiVolume(int id, unsigned char volch)
	{
		long pitchbendvalue = volch << 7;
		int lsb = pitchbendvalue & 0x7f;
		int msb = (pitchbendvalue & 0x7f00) >> 7;

		m_midiout->Send(PITCH_BEND + (id % 8), lsb, msb, -1);
	}
	//void SetSurfacePan(MediaTrack *trackid, double pan)
	//{
	//	FIXID(id)
	//		if (m_midiout && id >= 0 && id < 256 && id < m_size)
	//		{
	//			unsigned char panch = panToChar(pan);
	//			if (m_pan_lastpos[oid] != panch)
	//			{
	//				m_pan_lastpos[oid] = panch;

	//				//m_midiout->Send(0xb0 + id / 8, 0x1 + (id & 7), panch, -1);
	//				if (IsInCurrentControlView(id))
	//				{
	//					m_midiout->Send(0xb0, 0x1 + (id & 7), panch, -1);
	//				}
	//			}
	//		}
	//}



	void SetPlayState(bool play, bool pause, bool rec)
	{
		if (m_midiout)
		{
			if (play)
				m_midiout->Send(NOTE_ON, 94, 2, -1);  // PLAY-Button an
			if (pause)
				m_midiout->Send(NOTE_ON, 94, 1, -1);  // PLAY-Button blinkend
			if (!play && !pause)
				m_midiout->Send(NOTE_ON, 94, 0, -1);  // PLAY-Button aus
		}
	}


	void SendMidiSelect(int id, bool select)
	{
		m_midiout->Send(NOTE_ON, 24 + (id % 8), select ? 2 : 0, -1);
	}

	void ResetCachedVolPanStates()
	{
		memset(m_vol_lastpos, 0xff, sizeof(m_vol_lastpos));
		memset(m_pan_lastpos, 0xff, sizeof(m_pan_lastpos));
	}

	void OnTrackSelection(MediaTrack *trackid)
	{
		FIXID(id)

			// Ermitteln, in welcher Bank der Track sich befindet
			int bank_new = 0;
		for (; bank_new < 20; bank_new++)
		{
			if (id >= (0 + 8 * bank_new) && id <= (7 + 8 * bank_new))
			{
				break;
			}
		}

		if (bank_new != bank)
		{
			bank = bank_new;
			SaveBankShift();
			SetTrackListChange();
		}

		// Select-LED reset
		for (int x = 0; x < 8; x++)
		{
			SendMidiSelect(x, false);
		}
		// einzelne Select-LED anschalten
		SendMidiSelect(id, true);

	}

	bool IsInCurrentControlView(int id)
	{
		return id >= (0 + 8 * bank) && id <= (7 + 8 * bank);
	}

	bool IsKeyDown(int key)
	{
		return false;
	}

	void AdjustBankShift(bool forward)
	{
		if (forward)
		{
			bank++;
		}
		else
		{
			bank--;
		}

		if (m_midiout)
			m_midiout->Send(CC, 97, int2digit(bank), -1);

		SaveBankShift(true);
	}

	void SaveBankShift(bool markFirstTrackInBank = false)
	{

		int firstTrackId = bank * 8 + m_offset;

		// aktive Tracks
		for (int idx = firstTrackId, ctrack = 0; idx <= firstTrackId + 7; idx++)
		{
			MediaTrack *tr = CSurf_TrackFromID(idx, g_csurf_mcpmode);
			if (tr)
			{
				// Ersten Track im aktuellen View markieren
				if (ctrack == 0 && markFirstTrackInBank)
					SetOnlyTrackSelected(tr);

				if (m_midiout)
				{
					// Volume
					SendMidiVolume(ctrack, m_vol_lastpos[idx]);
					//m_midiout->Send(0xB0, 0x51 + (ctrack & 7), m_vol_lastpos[idx], -1);

					// Pan
					//m_midiout->Send(0xb0, 0x1 + (ctrack & 7), m_pan_lastpos[idx], -1);

					// Mute
					SendMidiMute(ctrack, m_mute_laststate[idx]);
					//m_midiout->Send(0xb0, 0x49 + (ctrack & 7), m_mute_laststate[idx] ? 0x7f : 0, -1);

					// Solo
					SendMidiSolo(ctrack, m_solo_laststate[idx]);
					//m_midiout->Send(0xb0, 0x41 + (ctrack & 7), m_solo_laststate[idx] ? 0x7f : 0, -1);
				}
			}
			else
			{
				// restliche Tracks bis zur vollen 8 leeren
				if (m_midiout)
				{
					// Volume
					SendMidiVolume(ctrack, 0);
					//m_midiout->Send(0xB0, 0x51 + (ctrack & 7), m_vol_lastpos[idx], -1);

					// Pan
					//m_midiout->Send(0xb0, 0x1 + (ctrack & 7), m_pan_lastpos[idx], -1);

					// Mute
					SendMidiMute(ctrack, false);
					//m_midiout->Send(0xb0, 0x49 + (ctrack & 7), m_mute_laststate[idx] ? 0x7f : 0, -1);

					// Solo
					SendMidiSolo(ctrack, false);
					//m_midiout->Send(0xb0, 0x41 + (ctrack & 7), m_solo_laststate[idx] ? 0x7f : 0, -1);
				}
			}

			ctrack++;
		}

		SetTrackListChange();
	}

};


static void parseParms(const char *str, int parms[4])
{
	parms[0] = 1; // Offset
	parms[1] = 128; // Max Anzahl tracks
	parms[2] = parms[3] = -1;

	const char *p = str;
	if (p)
	{
		int x = 0;
		while (x < 4)
		{
			while (*p == ' ') p++;
			if ((*p < '0' || *p > '9') && *p != '-') break;
			parms[x++] = atoi(p);
			while (*p && *p != ' ') p++;
		}
	}
}

static IReaperControlSurface *createFunc(const char *type_string, const char *configString, int *errStats)
{
	int parms[4];
	parseParms(configString, parms);

	return new CSurf_APCKeys25(parms[0], parms[1], parms[2], parms[3], errStats);
}


static WDL_DLGRET dlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		int parms[4];
		parseParms((const char *)lParam, parms);

		int n = GetNumMIDIInputs();
		int x = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)"None");
		SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETITEMDATA, x, -1);
		x = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)"None");
		SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETITEMDATA, x, -1);
		for (x = 0; x < n; x++)
		{
			char buf[512];
			if (GetMIDIInputName(x, buf, sizeof(buf)))
			{
				int a = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)buf);
				SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETITEMDATA, a, x);
				if (x == parms[2]) SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETCURSEL, a, 0);
			}
		}
		n = GetNumMIDIOutputs();
		for (x = 0; x < n; x++)
		{
			char buf[512];
			if (GetMIDIOutputName(x, buf, sizeof(buf)))
			{
				int a = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)buf);
				SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETITEMDATA, a, x);
				if (x == parms[3]) SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETCURSEL, a, 0);
			}
		}
		SetDlgItemInt(hwndDlg, IDC_EDIT1, parms[0], TRUE);
		SetDlgItemInt(hwndDlg, IDC_EDIT2, parms[1], FALSE);
	}
	break;
	case WM_USER + 1024:
		if (wParam > 1 && lParam)
		{
			char tmp[512];

			int indev = -1, outdev = -1, offs = 0, size = 9;
			int r = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_GETCURSEL, 0, 0);
			if (r != CB_ERR) indev = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_GETITEMDATA, r, 0);
			r = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_GETCURSEL, 0, 0);
			if (r != CB_ERR)  outdev = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_GETITEMDATA, r, 0);

			BOOL t;
			r = GetDlgItemInt(hwndDlg, IDC_EDIT1, &t, TRUE);
			if (t) offs = r;
			r = GetDlgItemInt(hwndDlg, IDC_EDIT2, &t, FALSE);
			if (t)
			{
				if (r < 1)r = 1;
				else if (r > 256)r = 256;
				size = r;
			}

			sprintf(tmp, "%d %d %d %d", offs, size, indev, outdev);
			lstrcpyn((char *)lParam, tmp, wParam);

		}
		break;
	}
	return 0;
}

static HWND configFunc(const char *type_string, HWND parent, const char *initConfigString)
{
	return CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_SURFACEEDIT_MCU), parent, dlgProc, (LPARAM)initConfigString);
}


reaper_csurf_reg_t csurf_APCKeys25_reg = { "X-Touch",	"X-Touch", createFunc, configFunc, };