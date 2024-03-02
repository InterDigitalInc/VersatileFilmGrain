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
from tkinter import (ttk, Menu, simpledialog, filedialog as fd)
import matplotlib
import numpy as np
import scipy.ndimage
import ctypes.wintypes
import math
from math import log2
import os
import re

matplotlib.use('TkAgg')
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import (FigureCanvasTkAgg,NavigationToolbar2Tk)
from matplotlib.backend_bases import MouseButton

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
		self.enable = \
			[[ True ]*self.num_intensity_intervals[0],
			 [ True ]*self.num_intensity_intervals[1],
			 [ True ]*self.num_intensity_intervals[2]]
		self.gain = 100

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

			self.enable[c] = [ True ]*self.num_intensity_intervals[c]
			self.gain = 100

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

	def save(self, filename, mask=False):
		with open(filename, 'w') as f:
			f.write('SEIFGCEnabled                          : 1\n')
			f.write('SEIFGCCancelFlag                       : 0\n')
			f.write('SEIFGCPersistenceFlag                  : 1\n')
			f.write(f'SEIFGCModelID                          : {self.model_id}\n')
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
					x = [self.comp_model_value[c][k].copy() for k in range(self.num_intensity_intervals[c])]
					for k in range(self.num_intensity_intervals[c]):
						if mask and not self.enable[c][k]:
							x[k][0] = 0
					f.write(f'SEIFGCCompModelValuesComp{c}             : {array2str2(x)}\n')

	def split(self,c,k,i):
		if (self.comp_model_present_flag[c] and k < self.num_intensity_intervals[c]):
			if (i > self.intensity_interval_lower_bound[c][k] and i <= self.intensity_interval_upper_bound[c][k]):
				self.intensity_interval_lower_bound[c].insert(k+1,i)
				self.intensity_interval_upper_bound[c].insert(k,i-1)
				self.comp_model_value[c].insert(k,self.comp_model_value[c][k].copy())
				self.enable[c].insert(k,self.enable[c][k])
				self.num_intensity_intervals[c] += 1

# ------------------------------------------------------------------------------
# YUV preview window
# ------------------------------------------------------------------------------

def read_yuv(filename, frame=0, width=1920, height=1080, bits=8, format=420):
	''' read one frame from a YUV file '''
	# TODO: detect size --> regex [non-number][3 or 4 numbers]x[3 or 4 numbers][non-number]
	# TODO: detect 10-bit
	# TODO: detect 422
	# Fallback: ask user
	t = 'uint16' if bits==10 else 'uint8'
	b = 2 if bits==10 else 1
	if format==400:
		cwidth = cheight = 0
		U = V = []
	else:
		cwidth = int(width / 2) if format==420 or format==422 else width
		cheight = int(height / 2) if format==420 else height
	ysize = width * height
	csize = cwidth * cheight
	psize = ysize + 2*csize
	offset = frame * psize
	Y = np.fromfile(filename, dtype=t, count=ysize, offset=offset*b).reshape(height,width)
	if cwidth:
		U = np.fromfile(filename, dtype=t, count=csize, offset=(offset+ysize)*b).reshape(cheight,cwidth)
		V = np.fromfile(filename, dtype=t, count=csize, offset=(offset+ysize+csize)*b).reshape(cheight,cwidth)
	return Y,U,V

def yuv444(Y, U, V):
	''' upscale chroma to luma size '''
	# Chroma upscale : horizontal (cosited)
	if 2*np.shape(U)[1] == np.shape(Y)[1]:
		f = np.sinc(np.arange(-1.5,1.5))
		f /= np.sum(f)
		sz = list(U.shape)
		sz[1] *= 2
		U = np.reshape(np.vstack((U, scipy.ndimage.correlate1d(U, f, axis=1, mode="nearest"))), sz, order='F')
		V = np.reshape(np.vstack((V, scipy.ndimage.correlate1d(V, f, axis=1, mode="nearest"))), sz, order='F')

	# Chroma upscale : vertical (midpoint)
	if 2*np.shape(U)[0] == np.shape(Y)[0]:
		f = np.sinc(np.arange(-1.75,1.25))
		f = np.append(f,0)
		f /= np.sum(f)
		sz = list(U.shape)
		sz[0] *= 2
		U = np.reshape(np.hstack((scipy.ndimage.correlate1d(U, f, axis=0, mode="nearest"), scipy.ndimage.correlate1d(U, np.flip(f), axis=0, mode="nearest"))), sz, order='C')
		V = np.reshape(np.hstack((scipy.ndimage.correlate1d(V, f, axis=0, mode="nearest"), scipy.ndimage.correlate1d(V, np.flip(f), axis=0, mode="nearest"))), sz, order='C')

	return Y, U, V

def yuv2rgb(Y, U, V):
	''' convert YUV to RGB '''
	# 1. Convert Y,U,V to full scale
	bits = 10 if Y.itemsize > 1 else 8
	D = 2**(bits-8)
	FR = 0 # not full range (= limited range, as usual for video)
	signed = False # chroma unsigned as usual
	Yrng = (219 + 36*FR)*D
	Yblk = (16 - 16*FR)*D
	Crng = (224 + 32*FR)*D
	Cmid = (128)*D if not signed else 0

	Y = (Y.astype(float) - Yblk) / Yrng
	U = (U.astype(float) - Cmid) / Crng
	V = (V.astype(float) - Cmid) / Crng

	# 2. Upscale chroma to luma resolution, taking care of luma/chroma alignment (by default: horizontal colocated, vertical centered)
	Y, U, V = yuv444(Y, U, V)

	# 3. Apply BT.709 matrix (no conversion of colors primaries or transfer function, no tone mapping)
	R = Y             + 1.57480*V
	G = Y - 0.18732*U - 0.46812*V
	B = Y + 1.85560*U
	rgb = np.stack((R, G, B),axis=2)

	# 4. clip
	rgb = np.clip(rgb, 0, 1)

	# Done. [note: dither defore display ?]
	return rgb

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
	def __init__(self, parent, clean_yuv):
		super().__init__(parent)
		self.title('Preview')
		self.fullscreen = False

		figure = Figure(figsize=(6, 4), dpi=100)
		self.figure_canvas = FigureCanvasTkAgg(figure, self)
		self.axes = figure.add_axes((0,0,1,1))
		self.axes.axis(False)
		#self.axes.axis([0,600,400,0]) # no zoom --> size of figure
		self.figure_canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)
		self.figure_canvas.mpl_connect('button_press_event', self.on_press)

		self.clean_yuv = clean_yuv
		self.clean_rgb = yuv2rgb(*clean_yuv)
		self.mode = 3 # TODO add a param ?
		self.regrain(clean_yuv)

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

	def regrain(self, grain_yuv):
		self.grain_yuv = grain_yuv
		self.grain_rgb = yuv2rgb(*grain_yuv)
		self.imshow()

	def imshow(self):
		Y, U, V = self.grain_yuv
		if self.mode==0:
			self.axes.imshow(Y, cmap='gray')
		elif self.mode==1:
			self.axes.imshow(U, cmap='gray')
		elif self.mode==2:
			self.axes.imshow(V, cmap='gray')
		else:
			self.axes.imshow(self.grain_rgb)
		# also consider clean/grainy
		self.figure_canvas.draw()

	def setmode(self, mode):
		self.mode = mode
		self.imshow()

#	def resize(zoom=1.0):
		# picture size
		# canvas/x size
		# change xy range ?
		# - if pic size smaller than canvas, center
		# - if not, center too (provide pivot point)

# ------------------------------------------------------------------------------
# Dialog: YUV file properties
# ------------------------------------------------------------------------------

class AskYuvInfo(tk.simpledialog.Dialog):
	def __init__(self, master, **kwargs):
		# parse text param, then override with explicit params
		width = height = depth = format = None;
		if 'text' in kwargs:
			text = kwargs.pop('text');
			m = re.search('\d{2,4}x\d{2,4}', text)
			if m:
				m = m.group().split('x')
				width = int(m[0])
				height = int(m[1])
			m = re.search('\d{1,2}(b[^A-Za-z0-9]?|bits)', text)
			if m:
				m = m.group().split('b')
				depth = int(m[0])
			m = re.search('[^A-Za-z0-9]?(420|422|444)p?[^A-za-z0-9]?', text)
			if m:
				format = int(m.group()[1:4])
		# default values
		if 'width'  in kwargs: width  = kwargs.pop('width')
		if not width         : width  = 1920
		if 'height' in kwargs: height = kwargs.pop('height')
		if not height        : height = 1080
		if 'depth'  in kwargs: depth  = kwargs.pop('depth')
		if not depth         : depth  = 8
		if 'format' in kwargs: format = kwargs.pop('format')
		if not format        : format = 420

		# init
		self.width = 0 # detect cancel
		self.__width  = tk.StringVar(value=width)
		self.__height = tk.StringVar(value=height)
		self.__depth  = tk.StringVar(value=depth)
		self.__format = tk.StringVar(value=format)
		tk.simpledialog.Dialog.__init__(self,master,**kwargs)

	def body(self, master):
		ttk.Label(master,text='Width',anchor="e").grid(column=0,row=0, sticky='swe')
		l = ttk.Combobox(master,values=['720','1280','1440','1920','2560','2880','3840'],textvariable=self.__width,width=10)
		l.grid(column=1,row=0, sticky='swe',padx=5)

		ttk.Label(master,text='Height',anchor="e").grid(column=0,row=1, sticky='swe')
		ttk.Combobox(master,values=['640','576','720','1080','1440','2160'],textvariable=self.__height,width=10).grid(column=1,row=1, sticky='swe',padx=5)

		ttk.Label(master,text='Bit depth',anchor="e").grid(column=0,row=2, sticky='swe')
		ttk.Combobox(master,values=['8','10'],textvariable=self.__depth,width=10).grid(column=1,row=2, sticky='swe',padx=5)

		ttk.Label(master,text='Chroma format',anchor="e").grid(column=0,row=3, sticky='swe')
		ttk.Combobox(master,state='readonly',values=['420','422','444'],textvariable=self.__format,width=10).grid(column=1,row=3, sticky='swe',padx=5)

		master.columnconfigure(0, weight=1, pad=5)
		master.columnconfigure(1, weight=1)
		master.rowconfigure(0, pad=5)
		master.rowconfigure(1, pad=3)
		master.rowconfigure(2, pad=3)
		master.rowconfigure(3, pad=3)

		return l # return widget that should have initial focus.

	def apply(self):
		self.width  = int(self.__width .get())
		self.height = int(self.__height.get())
		self.depth  = int(self.__depth .get())
		self.format = int(self.__format.get())

# ------------------------------------------------------------------------------
# Main app: interactive plot
# ------------------------------------------------------------------------------

class App(tk.Tk):
	def __init__(self):
		super().__init__()
		self.picked = []
		self.picked_split = []
		self.picked_k = []
		self.seed = 0xdeadbeef
		self.yuvname = []
		self.frame = tk.IntVar(value=0)

		self.title('FGC SEI designer [draft]')

		# create the figure
		figure = Figure(figsize=(6, 4), dpi=100)
		self.figure_canvas = FigureCanvasTkAgg(figure, self)
		NavigationToolbar2Tk(self.figure_canvas, self, pack_toolbar=False).grid(column=0, row=0, sticky='ew', columnspan=3)

		# create the menubar
		menubar = Menu(self)
		self.config(menu=menubar)
		# file menu
		file_menu = Menu(menubar, tearoff=0)
		file_menu.add_command(label='Load cfg', underline=0, command=self.on_load_cfg)
		file_menu.add_command(label='Save cfg', underline=0, command=self.on_save_cfg)
		file_menu.add_separator()
		file_menu.add_command(label='Load YUV (clean)', underline=10, command=self.on_load_yuv_clean)
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
		self.ax1.set_ylabel(f'Gain (x 2^{cfg.log2_scale_factor})')

		self.ax2 = self.ax1.twinx()
		self.ax2.axis([0,256,0,15])
		self.ax2.set_ylabel('Frequency cut-off', color='tab:green')
		self.ax2.tick_params(axis='y', labelcolor='tab:green')

		self.update_plot(0)

		self.figure_canvas.mpl_connect('button_press_event', self.on_press)
		self.figure_canvas.mpl_connect('motion_notify_event', self.on_motion)
		self.figure_canvas.mpl_connect('button_release_event', self.on_release)
#		self.figure_canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)
		self.figure_canvas.get_tk_widget().grid(column=0, row=1,columnspan=3, sticky='nswe')

		# sliders
		self.columnconfigure(2, weight=3)
		self.rowconfigure(1, weight=3)
		self.log2_scale_factor = tk.IntVar(value=cfg.log2_scale_factor)
		ttk.Label(self,text='Log2ScaleFactor').grid(column=0, row=2, sticky='w',padx=5) # TODO: same font as Matplotlib
		self.label1 = ttk.Label(self, text=f'{cfg.log2_scale_factor}');
		self.label1.grid(column=1, row=2, sticky='e',padx=5)
		ttk.Scale(self, from_=2, to=7, variable=self.log2_scale_factor, command=self.slider1_changed).grid(column=2, row=2, sticky='we',padx=5,pady=5) # TODO: snap

		ttk.Label(self,text='Global scale').grid(column=0, row=3, sticky='w',padx=5)
		self.gain = tk.DoubleVar(value=log2(cfg.gain/100.0))
		self.label2 = ttk.Label(self, text=f'{cfg.gain}', width=3, anchor="e")
		self.label2.grid(column=1, row=3, sticky='e',padx=5)
		ttk.Scale(self, from_=-1, to=1, variable=self.gain, command=self.slider2_changed).grid(column=2, row=3, sticky='we',padx=5,pady=5) # TODO: snap

		ttk.Label(self,text='YUV frame').grid(column=0, row=4, sticky='w',padx=5)
		self.label3 = ttk.Label(self, text=f'{self.frame.get()}', width=3, anchor="e")
		self.label3.grid(column=1, row=4, sticky='e',padx=5)
		self.scale3 = ttk.Scale(self, from_=0, to=0, variable=self.frame, command=self.slider3_changed)
		self.scale3.grid(column=2, row=4, sticky='we',padx=5,pady=5) # TODO: snap

		# TODO: align widgets & plot background color

		self.update_plot(0)

	def slider1_changed(self, event):
		x = self.log2_scale_factor.get()
		if x != cfg.log2_scale_factor:
			cfg.log2_scale_factor = self.log2_scale_factor.get()
			self.label1.configure(text=f'{cfg.log2_scale_factor}')
			self.ax1.set_ylabel(f'Gain (x 2^{cfg.log2_scale_factor})')
			self.figure_canvas.draw()
			self.regrain()

	def slider2_changed(self, event):
		x = round(pow(2,self.gain.get())*100)
		if (x != cfg.gain):
			cfg.gain = round(pow(2,self.gain.get())*100)
			self.label2.configure(text=f'{cfg.gain}')
			self.regrain()

	def slider3_changed(self, event):
		self.label3.configure(text=f'{self.frame.get()}')
		self.seed = ((self.seed + 1) & 0xffffffff)
		self.regrain()

	# Select color component
	def on_color_component(self):
		self.update_plot(self.color_component.get())
		self.figure_canvas.draw()

	def on_load_cfg(self):
		filename = fd.askopenfilename(title="Choose an FGC SEI config file (VTM format)",filetype=[('cfg files','.cfg'),('all files','.*')])
		if (filename):
			cfg.load(filename)
			self.log2_scale_factor.set(cfg.log2_scale_factor)
			self.label1.configure(text=f'{cfg.log2_scale_factor}')
			self.ax1.set_ylabel(f'Gain (x 2^{cfg.log2_scale_factor})')
			self.gain.set(0)
			# TODO: restore previous if load failed ?
			#self.color_component.set(0)
			self.update_plot(self.color_component.get())
			self.figure_canvas.draw()
			self.regrain()

	def on_save_cfg(self):
		filename = fd.asksaveasfilename(title="Save FGC SEI config (VTM format) as",filetypes=[('cfg files','.cfg')])
		if (filename):
			cfg.save(filename)

	def on_load_yuv_clean(self):
		self.yuvname = fd.askopenfilename(title="Choose YUV file (clean/compressed)",filetype=[('YUV files','.yuv'),('all files','.*')])
		self.yuvsize = 0
		if (self.yuvname):
			dlg = AskYuvInfo(self,text=self.yuvname)
			if dlg.width:
				self.yuvinfo = [dlg.width, dlg.height, dlg.depth, dlg.format]
				self.yuv_clean = read_yuv(self.yuvname, self.frame.get(), *self.yuvinfo)
				psize = self.yuv_clean[0].size + self.yuv_clean[1].size + self.yuv_clean[2].size
				psize *= self.yuv_clean[0].itemsize
				self.yuvsize = os.path.getsize(self.yuvname)/psize
				self.seed = ((self.seed + 1) & 0xffffffff)
				#preview = Preview(self, self.yuv_clean); # TODO: just update if already open !
				self.regrain()
			else:
				self.yuvname= []

		# update slide 3 dimension
		self.scale3.configure(to=self.yuvsize - 1);

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

			# Record which interval is clicked (special click to disable interval, see on_release() for right click = toggle an 'enable' flag)
			self.picked_k = [k for k,a in enumerate(self.linesG) if event.xdata >= a.get_xdata()[0] and event.xdata < a.get_xdata()[1]][0:1]

			# Double-click in a interval --> split
			self.picked_split = [round(event.xdata),] if self.picked_k and event.dblclick and event.button is MouseButton.LEFT else [] # floor instead ?

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
			self.regrain()

	# Mouse release: update cfg (for split), update plot (for split & merge)
	def on_release(self, event):
		c = self.color_component.get()
		if (self.picked_split):
			cfg.split(0, self.picked_k[0], self.picked_split[0])
			self.picked_split = []
		elif not self.picked and self.picked_k and event.button is MouseButton.RIGHT:
			k = self.picked_k[0]
			cfg.enable[c][k] = not cfg.enable[c][k]
			self.picked_k = []
			self.regrain()

		self.picked = []
		self.update_plot(c)
		self.figure_canvas.draw()

	# Update cfg from plot
	def update_cfg(self, c):
		i = 0
		for k,a in enumerate(self.linesA):
			L, H = self.linesG[k].get_xdata()
			if (H > L):
				cfg.intensity_interval_lower_bound[c][i] = L
				cfg.intensity_interval_upper_bound[c][i] = H - 1
				cfg.enable[c][i] = self.enable[k]
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
		del cfg.enable[c][i:]

	def regrain(self):
		if (self.yuvname):
			cfg.save('__preview.cfg',mask=True);
			os.system(f'vfgs -w {self.yuvinfo[0]} -h {self.yuvinfo[1]} -b {self.yuvinfo[2]} --outdepth 8 -n 1 -s {self.frame.get()} -r {self.seed} -g {cfg.gain} -c __preview.cfg {self.yuvname} __preview_{self.yuvinfo[0]}x{self.yuvinfo[1]}_8b.yuv')

	# Update plot from cfg
	def update_plot(self, c):
		# clear plot + saved lines
		for l in self.ax1.get_lines() + self.ax2.get_lines():
			l.remove()
		self.linesA = []
		self.linesB = []
		self.linesG = []
		self.linesF = []
		self.enable = []

		for k in range(cfg.num_intensity_intervals[c]):
			self.enable.append(cfg.enable[c][k])
			alpha = 1 if self.enable[k] else 0.1
			a = cfg.intensity_interval_lower_bound[c][k]
			b = cfg.intensity_interval_upper_bound[c][k]+1
			g = cfg.comp_model_value[c][k][0]
			line, = self.ax1.plot([a,a], [0,g],'b--', alpha=alpha, picker=5)
			self.linesA.append(line)
			line, = self.ax1.plot([b,b], [0,g],'b--', alpha=alpha, picker=5)
			self.linesB.append(line)
			line, = self.ax1.plot([a,b], [g,g],'bo-', alpha=alpha, picker=5)
			self.linesG.append(line)
			if (cfg.num_model_values[c] > 1 and cfg.model_id==0):
				f = cfg.comp_model_value[c][k][1]
				line, = self.ax2.plot([a,b], [f,f],'s-', color='tab:green', alpha=alpha, picker=5)
				self.linesF.append(line)

		self.ax1.set_xlabel(('Y','Cb','Cr')[c])


if __name__ == '__main__':
	cfg = FgcSei()
	app = App()
	app.mainloop()

