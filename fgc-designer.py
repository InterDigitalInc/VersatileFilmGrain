#!/usr/bin/env python

# The copyright in this software is being made available under the BSD
# License, included below. This software may be subject to other third party
# and contributor rights, including patent rights, and no such rights are
# granted under this license.
#
# Copyright (c) 2022-2023, InterDigital
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted (subject to the limitations in the disclaimer below) provided that
# the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of InterDigital nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS
# LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import configparser
import tkinter as tk
from tkinter import (ttk, Menu, filedialog as fd)
import matplotlib
import numpy as np
import ctypes.wintypes

matplotlib.use('TkAgg')
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import (FigureCanvasTkAgg,NavigationToolbar2Tk)

# ------------------------------------------------------------------------------
# FGC SEI cfg read/write
# ------------------------------------------------------------------------------

def array2str(array):
	s = ""
	if len(array):
		s = str(array[0])
	if len(array)>1:
		for x in array[1:]:
			s = s + " " + str(x)
	return s

def array2str2(array):
	return array2str([array2str(x) for x in array])

class FgcSei:
	# TODO:
	# - from/to dictionnary ?
	# - copy with global scale / with interval mask

	def __init__(self):
		self.reset()

	def reset(self):
		# reset to default configuration
		self.model_id = 0
		self.log2_scale_factor = 5
		self.comp_model_present_flag = [ 1, 1, 1 ]
		self.num_intensity_intervals = [ 8, 8, 8 ]
		self.num_model_values = [ 3, 3, 3 ]
		self.intensity_interval_lower_bound = \
			[[  0, 40,  60,  80, 100, 120, 140, 160 ],
			 [  0, 64,  96, 112, 128, 144, 160, 192 ],
			 [  0, 64,  96, 112, 128, 144, 160, 192 ]]
		self.intensity_interval_upper_bound = \
			[[ 39, 59,  79,  99, 119, 139, 159, 255 ],
			 [ 63, 95, 111, 127, 143, 159, 191, 255 ],
			 [ 63, 95, 111, 127, 143, 159, 191, 255 ]]
		self.comp_model_value = [ \
			# luma (scale / h / v)
			   [[ 100,  7,  7 ],
				[ 100,  8,  8 ],
				[ 100,  9,  9 ],
				[ 110, 10, 10 ],
				[ 120, 11, 11 ],
				[ 135, 12, 12 ],
				[ 145, 13, 13 ],
				[ 180, 14, 14 ]],
			# Cb
			   [[ 128, 8, 8 ],
				[  96, 8, 8 ],
				[  64, 8, 8 ],
				[  64, 8, 8 ],
				[  64, 8, 8 ],
				[  64, 8, 8 ],
				[  96, 8, 8 ],
				[ 128, 8, 8 ]],
			# Cr
			   [[ 128, 8, 8 ],
				[  96, 8, 8 ],
				[  64, 8, 8 ],
				[  64, 8, 8 ],
				[  64, 8, 8 ],
				[  64, 8, 8 ],
				[  96, 8, 8 ],
				[ 128, 8, 8 ]]]

	def load(self, filename):
		parser = configparser.ConfigParser(delimiters=(":"),comment_prefixes=('#'),inline_comment_prefixes=('#'))

		# parse file
		with open(filename) as stream:
			parser.read_string("[root]\n" + stream.read())

		# copy data
		if parser.has_option("root","SEIFGCModelID"):
			self.model_id = parser.getint("root","SEIFGCModelID")
		if parser.has_option("root","SEIFGCLog2ScaleFactor"):
			self.log2_scale_factor = parser.getint("root","SEIFGCLog2ScaleFactor")
		for c in range(3):
			if parser.has_option("root",f"SEIFGCCompModelPresentComp{c}"):           self.comp_model_present_flag[c] = parser.getint("root",f"SEIFGCCompModelPresentComp{c}")
		for c in range(3):
			if self.comp_model_present_flag[c]:
				if parser.has_option("root",f"SEIFGCNumIntensityIntervalMinus1Comp{c}"):
					self.num_intensity_intervals[c] = parser.getint("root",f"SEIFGCNumIntensityIntervalMinus1Comp{c}") + 1
				if parser.has_option("root",f"SEIFGCNumModelValuesMinus1Comp{c}"):
					self.num_model_values[c] = parser.getint("root",f"SEIFGCNumModelValuesMinus1Comp{c}") + 1
				if parser.has_option("root",f"SEIFGCIntensityIntervalLowerBoundComp{c}"):
					self.intensity_interval_lower_bound[c] = list(map(int,parser.get("root",f"SEIFGCIntensityIntervalLowerBoundComp{c}").split()))
				if parser.has_option("root",f"SEIFGCIntensityIntervalUpperBoundComp{c}"):
					self.intensity_interval_upper_bound[c] = list(map(int,parser.get("root",f"SEIFGCIntensityIntervalUpperBoundComp{c}").split()))
				# TODO: check length of lists (intervals)
				if parser.has_option("root",f"SEIFGCCompModelValuesComp{c}"):
					data = np.array(list(map(int,parser.get("root",f"SEIFGCCompModelValuesComp{c}").split())))
					data = data.reshape(self.num_intensity_intervals[c], -1)
					self.comp_model_value[c] = data.tolist()
					# TODO: check length of lists (intervals x model values)
					# TODO: reset model_values lists to the right length (or use np arrays & reshape)
					# TODO: check range of values
			else:
				self.num_intensity_intervals[c] = 0

		return 0

	def cat(self):
		print(f'model_id                          : {self.model_id}')
		print(f'log2_scale_factor                 : {self.log2_scale_factor}')
		print(f'comp_model_present_flag           : {self.comp_model_present_flag}')
		print(f'num_intensity_intervals           : {self.num_intensity_intervals}')
		print(f'num_model_values                  : {self.num_model_values}')
		for k in range(3):
			if self.comp_model_present_flag[k]:
				print(f'intensity_interval_lower_bound[{k}] : {self.intensity_interval_lower_bound[k]}')
		for k in range(3):
			if self.comp_model_present_flag[k]:
				print(f'intensity_interval_upper_bound[{k}] : {self.intensity_interval_upper_bound[k]}')
		for k in range(3):
			if self.comp_model_present_flag[k]:
				print(f'comp_model_value[{k}]               : {self.comp_model_value[k]}')

	def save(self, filename):
		with open(filename, 'w') as f:
			f.write('SEIFGCEnabled                          : 1\n')
			f.write('SEIFGCCancelFlag                       : 0\n')
			f.write('SEIFGCPersistenceFlag                  : 1\n')
			f.write('SEIFGCModelID                          : {self.model_id}\n')
			f.write('SEIFGCSepColourDescPresentFlag         : 0\n')
			f.write('SEIFGCBlendingModeID                   : 0\n')
			f.write(f'SEIFGCLog2ScaleFactor                  : {self.log2_scale_factor}\n')
			for c in range(3):
				f.write(f'SEIFGCCompModelPresentComp{c}            : {self.comp_model_present_flag[c]}\n')
			for c in range(3):
				if self.comp_model_present_flag[c]:
					f.write(f'SEIFGCNumIntensityIntervalMinus1Comp{c}  : {self.num_intensity_intervals[c]-1}\n')
			for c in range(3):
				if self.comp_model_present_flag[c]:
					f.write(f'SEIFGCNumModelValuesMinus1Comp{c}        : {self.num_model_values[c]-1}\n')
			for c in range(3):
				if self.comp_model_present_flag[c]:
					f.write(f'SEIFGCIntensityIntervalLowerBoundComp{c} : {array2str(self.intensity_interval_lower_bound[c])}\n')
			for c in range(3):
				if self.comp_model_present_flag[c]:
					f.write(f'SEIFGCIntensityIntervalUpperBoundComp{c} : {array2str(self.intensity_interval_upper_bound[c])}\n')
			for c in range(3):
				if self.comp_model_present_flag[c]:
					f.write(f'SEIFGCCompModelValuesComp{c}             : {array2str2(self.comp_model_value[c])}\n')

	def split(self,c,k,i):
		if (self.comp_model_present_flag[c] and k < self.num_intensity_intervals[c]):
			if (i > self.intensity_interval_lower_bound[c][k] and i <= self.intensity_interval_upper_bound[c][k]):
				self.intensity_interval_lower_bound[c].insert(k+1,i)
				self.intensity_interval_upper_bound[c].insert(k,i-1)
				self.comp_model_value[c].insert(k,self.comp_model_value[c][k].copy())
				self.num_intensity_intervals[c] += 1

# ------------------------------------------------------------------------------
# YUV preview window
# ------------------------------------------------------------------------------

def read_yuv(filename, width=1920, height=1080, bits=10, format=420):
	''' read one frame from a YUV file '''
	Y = np.fromfile(filename, dtype='uint16', count=width*height, offset=0).reshape(height,width)
	return Y

def get_monitors_info():
	''' Obtain all monitors information and return information for the second monitor '''
	''' Windows only - using user32 eliminates the need to install the pywin32 software package '''
	user32 = ctypes.windll.user32
	def _get_monitors_resolution():
		monitors = []
		monitor_enum_proc = ctypes.WINFUNCTYPE(
		    ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.POINTER(ctypes.wintypes.RECT), ctypes.c_double)
		# Callback function,to obtain information for each display
		def callback(hMonitor, hdcMonitor, lprcMonitor, dwData):
			monitors.append((lprcMonitor.contents.left, lprcMonitor.contents.top,
			                 lprcMonitor.contents.right - lprcMonitor.contents.left,
			                 lprcMonitor.contents.bottom - lprcMonitor.contents.top))
			return 1
		# Enumerate all Monitors
		user32.EnumDisplayMonitors(None, None, monitor_enum_proc(callback), 0)
		return monitors
	# All monitors information
	monitors = _get_monitors_resolution()
	return monitors

class Preview(tk.Toplevel):
	def __init__(self, parent, Y):
		super().__init__(parent)
		self.title('Preview')
		self.fullscreen = False

		figure = Figure(figsize=(6, 4), dpi=100)
		figure_canvas = FigureCanvasTkAgg(figure, self)
		self.axes = figure.add_axes((0,0,1,1))
		self.axes.axis(False)
		figure_canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)
		figure_canvas.mpl_connect('button_press_event', self.on_press)

		# show image
		self.axes.imshow(Y, cmap='gray')

	def on_press(self, event):
		if event.inaxes and event.dblclick:
			# Toggle full-screen
			if self.fullscreen:
				#self.attributes("-fullscreen", False)
				self.state("normal");
				self.geometry(self.geo)
				self.overrideredirect(False) # Full screen without decorations
				self.fullscreen = False
			else:
				self.geo = self.winfo_geometry()
				[x,y] = [int(self.winfo_x()), int(self.winfo_y())]
				monitors = get_monitors_info()
				# search on which monitor we are, zoom on the same
				for k,m in enumerate(monitors):
					if x >= m[0] and x < m[0]+m[2] and y >= m[1] and y < m[1]+m[3]:
						break
				if k >= len(monitors):
					k = 0
				self.geometry(f"{monitors[k][2]}x{monitors[k][3]}+{monitors[k][0]}+{monitors[k][1]}")
				self.overrideredirect(True) # Full screen without decorations
				self.state("zoomed") # Windows specific
				#self.attributes("-fullscreen", True)
				self.fullscreen = True

# ------------------------------------------------------------------------------
# Main app: interactive plot
# ------------------------------------------------------------------------------

class App(tk.Tk):
	def __init__(self):
		super().__init__()
		self.picked = []
		self.picked_split = []

		self.title('FGC SEI designer [draft]')

		# create the figure
		figure = Figure(figsize=(6, 4), dpi=100)
		self.figure_canvas = FigureCanvasTkAgg(figure, self)
		NavigationToolbar2Tk(self.figure_canvas, self)

		# create the menubar
		menubar = Menu(self)
		self.config(menu=menubar)
		# file menu
		file_menu = Menu(menubar, tearoff=0)
		file_menu.add_command(label='Load cfg', underline=0, command=self.on_load_cfg)
		file_menu.add_command(label='Save cfg', underline=0, command=self.on_save_cfg)
		#file_menu.add_separator()
		#file_menu.add_command(label='Load YUV (clean)', underline=10, command=self.on_load_yuv_clean)
		#file_menu.add_command(label='Load YUV (grainy/original)', underline=10)
		file_menu.add_separator()
		file_menu.add_command(label='Exit', command=self.quit, underline=0)
		menubar.add_cascade(label="File", menu=file_menu, underline=0)
		# color component menu
		self.color_component = tk.IntVar(value=0)
		color_menu = Menu(menubar, tearoff=0)
		color_menu.add_radiobutton(label='Luma', underline=0, variable=self.color_component, value=0, command=self.on_color_component)
		color_menu.add_radiobutton(label='Cb', underline=1, variable=self.color_component, value=1, command=self.on_color_component) # TODO: "radio" style
		color_menu.add_radiobutton(label='Cr', underline=1, variable=self.color_component, value=2, command=self.on_color_component)
		menubar.add_cascade(label="Color component", menu=color_menu, underline=0)

		# create axes
		self.ax1 = figure.add_subplot()
		self.ax1.axis([0,256,0,255])
		self.ax1.set_ylabel('Gain')

		self.ax2 = self.ax1.twinx()
		self.ax2.axis([0,256,0,15])
		self.ax2.set_ylabel('Frequency cut-off', color='tab:green')
		self.ax2.tick_params(axis='y', labelcolor='tab:green')

		self.update_plot(0)

		self.figure_canvas.mpl_connect('button_press_event', self.on_press)
		self.figure_canvas.mpl_connect('motion_notify_event', self.on_motion)
		self.figure_canvas.mpl_connect('button_release_event', self.on_release)
		self.figure_canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

	# Select color component
	def on_color_component(self):
		self.update_plot(self.color_component.get())
		self.figure_canvas.draw()

	def on_load_cfg(self):
		filename = fd.askopenfilename(title="Choose an FGC SEI config file (VTM format)",filetype=[('cfg files','.cfg'),('all files','.*')])
		if (filename):
			cfg.load(filename)
			# TODO: restore previous if load failed ?
			#self.color_component.set(0)
			self.update_plot(self.color_component.get())
			self.figure_canvas.draw()

	def on_save_cfg(self):
		filename = fd.asksaveasfilename(title="Save FGC SEI config (VTM format) as",filetypes=[('cfg files','.cfg')])
		if (filename):
			cfg.save(filename)

	def on_load_yuv_clean(self):
		filename = fd.askopenfilename(title="Choose YUV file (clean/compressed)",filetype=[('YUV files','.yuv'),('all files','.*')])
		if (filename):
			Y = read_yuv(filename);
			preview = Preview(self, Y);

	# Mouse click: pick some items to be dragged
	def on_press(self, event):
		if event.inaxes:
			# Picked lower bound of intensity interval ?
			picked = [k for k,a in enumerate(self.linesA) if a.contains(event)[0]]
			self.picked_L = picked[0:1] # Keep only one
			# if picked: grab first H with same I
			if picked:
				I = self.linesA[picked[0]].get_xdata()[0]
				picked = [k for k,a in enumerate(self.linesB) if a.get_xdata()[0]==I]

			# Picked lower bound of intensity interval ?
			if not picked:
				picked = [k for k,a in enumerate(self.linesB) if a.contains(event)[0]]
			self.picked_H = picked[0:1] # Keep only one
			# if H picked and no L: grab first L with same I
			if picked and not self.picked_L:
				I = self.linesB[picked[0]].get_xdata()[0]
				picked = [k for k,a in enumerate(self.linesA) if a.get_xdata()[0]==I]
				self.picked_L = picked[0:1]

			# Compute change limits for picked bounds
			picked_I = self.picked_L + self.picked_H
			self.picked_I_lim = [0, 256]
			# min
			if (self.picked_H): # if picked an upper bound, just take the lower bound of the same interval
				self.picked_I_lim[0] = self.linesA[self.picked_H[0]].get_xdata()[0]
			elif (self.picked_L): # otherwise take anything just below
				I = self.linesA[self.picked_L[0]].get_xdata()[0]
				self.picked_I_lim[0] = max([line.get_xdata()[0] for line in self.linesA+self.linesB if line.get_xdata()[0] < I]+[0])
			# max
			if (self.picked_L): # if picked a lower bound, just take the upper bound of the same interval
				self.picked_I_lim[1] = self.linesB[self.picked_L[0]].get_xdata()[0]
			elif (self.picked_H): # otherwise take anything just above
				I = self.linesB[self.picked_H[0]].get_xdata()[0]
				self.picked_I_lim[1] = min([line.get_xdata()[0] for line in self.linesA+self.linesB if line.get_xdata()[0] > I]+[256])

			# Picked a gain ?
			picked = [k for k,a in enumerate(self.linesG) if a.contains(event)[0]]
			if picked_I: # then restrict picked to the intersection
				picked = [k for k in picked if k in picked_I]
			self.picked_G = picked

			# Picked a frequency ?
			self.picked_F = []
			if not self.picked_G: # Make selection of G and F mutually exclusive
				picked = [k for k,a in enumerate(self.linesF) if a.contains(event)[0]]
				if picked_I: # then restrict picked to the intersection
					picked = [k for k in picked if k in picked_I]
				self.picked_F =  picked

			# TODO: when testing on YUV, special click to disable interval (= additional 'enable' data in self)
			self.picked_k = [k for k,a in enumerate(self.linesG) if event.xdata >= a.get_xdata()[0] and event.xdata < a.get_xdata()[1]][0:1]

			# Double-click in a interval --> split
			self.picked_split = [round(event.xdata),] if self.picked_k and event.dblclick else [] # floor instead ?

			# Helper flag = something is picked
			self.picked = picked_I + self.picked_G + self.picked_F

	# Helper function to get I/G/F from x,y
	def get_model_values(self, x, y):
		# Get intensity and gain (ax1)
		_x, _y = self.ax1.transData.inverted().transform((x,y))
		I = round(_x)
		G = round(_y)
		# Get frequency (ax2)
		_x, _y = self.ax2.transData.inverted().transform((x,y))
		F = round(_y)
		return I, G, F

	# Mouse move: drag what was picked
	def on_motion(self, event):
		if event.inaxes and self.picked:
			I, G, F = self.get_model_values(event.x, event.y)
			# clip to valid values
			I = max(self.picked_I_lim[0], min(self.picked_I_lim[1], I))
			F = max(2, F)

			for k in self.picked_L:
				self.linesA[k].set_xdata([I,I])
				x = self.linesG[k].get_xdata()
				x[0] = I
				self.linesG[k].set_xdata(x)
				self.linesF[k].set_xdata(x)
			for k in self.picked_H:
				self.linesB[k].set_xdata([I,I])
				x = self.linesG[k].get_xdata()
				x[1] = I
				self.linesG[k].set_xdata(x)
				self.linesF[k].set_xdata(x)
			for k in self.picked_G:
				self.linesG[k].set_ydata([G,G])
				y = self.linesA[k].get_ydata()
				y[1] = G
				self.linesA[k].set_ydata(y)
				self.linesB[k].set_ydata(y)
			for k in self.picked_F:
				self.linesF[k].set_ydata([F,F])
			self.figure_canvas.draw()
			self.update_cfg(self.color_component.get())
			# TODO: update YUV (send event ?)

	# Mouse release: update cfg (for split), update plot (for split & merge)
	def on_release(self, event):
		self.picked = []
		if (self.picked_split):
			cfg.split(0, self.picked_k[0], self.picked_split[0])
			self.picked_split = []
		self.update_plot(self.color_component.get())
		self.figure_canvas.draw()

	# Update cfg from plot
	def update_cfg(self, c):
		i = 0
		for k,a in enumerate(self.linesA):
			L, H = self.linesG[k].get_xdata()
			if (H > L):
				cfg.intensity_interval_lower_bound[c][i] = L
				cfg.intensity_interval_upper_bound[c][i] = H - 1
				G = self.linesG[k].get_ydata()[0]
				cfg.comp_model_value[c][i][0] = G
				if (cfg.num_model_values[c] > 1 and cfg.model_id==0):
					F = self.linesF[k].get_ydata()[0]
					cfg.comp_model_value[c][i][1] = F
				if (cfg.num_model_values[c] > 2 and cfg.model_id==0):
					cfg.comp_model_value[c][i][2] = F # TODO: adjust Fh/Fv with aspect ratio slider
				i += 1
		cfg.num_intensity_intervals[c] = i
		del cfg.intensity_interval_lower_bound[c][i:]
		del cfg.intensity_interval_upper_bound[c][i:]
		del cfg.comp_model_value[c][i:]

	# Update plot from cfg
	def update_plot(self, c):
		# clear plot + saved lines
		for l in self.ax1.get_lines() + self.ax2.get_lines():
			l.remove()
		self.linesA = []
		self.linesB = []
		self.linesG = []
		self.linesF = []

		for k in range(cfg.num_intensity_intervals[c]):
			a = cfg.intensity_interval_lower_bound[c][k]
			b = cfg.intensity_interval_upper_bound[c][k]+1
			g = cfg.comp_model_value[c][k][0]
			line, = self.ax1.plot([a,a], [0,g],'b--', picker=5)
			self.linesA.append(line)
			line, = self.ax1.plot([b,b], [0,g],'b--', picker=5)
			self.linesB.append(line)
			line, = self.ax1.plot([a,b], [g,g],'bo-', picker=5)
			self.linesG.append(line)
			if (cfg.num_model_values[c] > 1 and cfg.model_id==0):
				f = cfg.comp_model_value[c][k][1]
				line, = self.ax2.plot([a,b], [f,f],'s-', color='tab:green', picker=5)
				self.linesF.append(line)

		self.ax1.set_xlabel(('Y','Cb','Cr')[c])


if __name__ == '__main__':
	cfg = FgcSei()
	app = App()
	app.mainloop()

