; **********************************************************************
; *  Color cycling animation script
; *  Daniel Cotting (cotting@mygale.org)
; **********************************************************************
; *  Official homepages: http://www.mygale.org/~cotting
; *                      http://cotting.citeweb.net
; *                      http://village.cyberbrain.com/cotting
; **********************************************************************    
; Makes a copy of your image and creates an animation of the active layer
; with the help of the alienmap plug-in. The animation may be saved with 
; the gif-plug-in. 
; **********************************************************************
; It is recommended to start the alienmap plug-in, to fiddle about with
; the parameters until you have found optimal start va lues. Then put 
; these values in the appropriate edit fields in the animation dialog.
; Now you can change the values in the alienmap plug-in to find interes-
; ting end values. After you have inserted these new values in the cor-
; responding fields of the animation script, you can start the calcula-
; tion of the animation. The script will create a new picture with a la-
; yer for each animation frame.    With each new frame the start values 
; will gradually turn into the specified end values, creating an amazing
; effect of color cycling.
; **********************************************************************


; The GIMP -- an image manipulation program
; Copyright (C) 1995 Spencer Kimball and Peter Mattis
; 
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.  
; 
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
; 
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
;
;
; **********************************************************************
; Original file:
; waves-anim.scm   version 1.00   09/04/97
; Copyright (C) 1997 Sven Neumann (neumanns@uni-duesseldorf.de)
; **********************************************************************
; 
;  

(define (script-fu-colorcycling-anim img
			       drawable
		 num-frames
		 startredstretch
		 startgreenstretch
		 startbluestretch
		 endredstretch
		 endgreenstretch
		 endbluestretch
		 redmode
		 greenmode
		 bluemode
		 redinvert
		 greeninvert
		 blueinvert
		 startredphase
		 startgreenphase
		 startbluephase
		 endredphase
		 endgreenphase
		 endbluephase
		 startredfrequency
		 startgreenfrequency
		 startbluefrequency
		 endredfrequency
		 endgreenfrequency
		 endbluefrequency
		 redinvert2
		 greeninvert2
		 blueinvert2)
  (let* ((startredstretch (max 0 startredstretch))
         (startgreenstretch (max 0 startgreenstretch))
	 (startbluestretch (max 0 startbluestretch))
	 (startredstretch (min 128 startredstretch))
         (startgreenstretch (min 128 startgreenstretch))
	 (startbluestretch (min 128 startbluestretch))
	 (endredstretch (max 0 endredstretch))
         (endgreenstretch (max 0 endgreenstretch))
	 (endbluestretch (max 0 endbluestretch))
	 (endredstretch (min 128 endredstretch))
         (endgreenstretch (min 128 endgreenstretch))
 	 (endbluestretch (min 128 endbluestretch))
	 
	 (redmode (max 0 redmode))
	 (redmode (min 2 redmode))
	 (greenmode (max 0 greenmode))
	 (greenmode (min 2 greenmode))
	 (bluemode (max 0 bluemode))
	 (bluemode (min 2 bluemode))

	 (startredfrequency (max 0 startredfrequency))
         (startgreenfrequency (max 0 startgreenfrequency))
	 (startbluefrequency (max 0 startbluefrequency))

	 (endredfrequency (max 0 endredfrequency))
         (endgreenfrequency (max 0 endgreenfrequency))
	 (endbluefrequency (max 0 endbluefrequency))
	 	 
	 (num-frames (max 1 num-frames))
	 (remaining-frames num-frames)
	 
	 (redstretch startredstretch)
	 (greenstretch startgreenstretch)
	 (bluestretch startbluestretch)
	 (redphase startredphase)
	 (greenphase startgreenphase)
	 (bluephase startbluephase)
	 (redfrequency startredfrequency)
	 (greenfrequency startgreenfrequency)
	 (bluefrequency startbluefrequency)
	 
	 (redstretchshift (/ (- endredstretch startredstretch) num-frames))
	 (greenstretchshift (/ (- endgreenstretch startgreenstretch) num-frames))
	 (bluestretchshift (/ (- endbluestretch startbluestretch) num-frames))
	 (redphaseshift (/ (- endredphase startredphase) num-frames))
	 (greenphaseshift (/ (- endgreenphase startgreenphase) num-frames))
	 (bluephaseshift (/ (- endbluephase startbluephase) num-frames))
	 (redfrequencyshift (/ (- endredfrequency startredfrequency) num-frames))
	 (greenfrequencyshift (/ (- endgreenfrequency startgreenfrequency) num-frames))
	 (bluefrequencyshift (/ (- endbluefrequency startbluefrequency) num-frames))

         (image (car (gimp-channel-ops-duplicate img))))
   
  (gimp-image-undo-disable image)

;  (if (= invert TRUE)
;      (set! phaseshift (- 0 phaseshift)))

  (set! source-layer (car (gimp-image-get-active-layer image)))
  
  (while (> remaining-frames 1)
         (set! alienmap-layer (car (gimp-layer-copy source-layer TRUE)))
         (gimp-layer-set-preserve-trans alienmap-layer FALSE)
	 (gimp-image-add-layer image alienmap-layer -1)
	 (set! layer-name (string-append "Frame "
					 (number->string
					  (- (+ num-frames 2)
					     remaining-frames) 10)))
	 (gimp-layer-set-name alienmap-layer layer-name)
	 
	 (plug-in-alienmap 1
		 image
		 alienmap-layer
		 redstretch
		 greenstretch
		 bluestretch
		 redmode
		 greenmode
		 bluemode
		 redinvert
		 greeninvert
		 blueinvert
		 redphase
		 greenphase
		 bluephase
		 redfrequency
		 greenfrequency
		 bluefrequency
		 redinvert2
		 greeninvert2
		 blueinvert2)

	; Huh ? way too much arguments???
	; Why does no STATUS_CALLING_ERROR occur?
	  
	 (set! remaining-frames (- remaining-frames 1))
	 (set! redphase (+ redphase redphaseshift))
	 (set! greenphase (+ greenphase greenphaseshift))
	 (set! bluephase (+ bluephase bluephaseshift))
	 (set! redfrequency (+ redfrequency redfrequencyshift))
	 (set! greenfrequency (+ greenfrequency greenfrequencyshift))
	 (set! bluefrequency (+ bluefrequency bluefrequencyshift))
	 (set! redstretch (+ redstretch redstretchshift))
	 (set! greenstretch (+ greenstretch greenstretchshift))
	 (set! bluestretch (+ bluestretch bluestretchshift))
	 
  )

  (gimp-layer-set-name source-layer "Frame 1")
  (plug-in-alienmap 1
		 image
		 source-layer
		 redstretch
		 greenstretch
		 bluestretch
		 redmode
		 greenmode
		 bluemode
		 redinvert
		 greeninvert
		 blueinvert
		 redphase
		 greenphase
		 bluephase
		 redfrequency
		 greenfrequency
		 bluefrequency
		 redinvert2
		 greeninvert2
		 blueinvert2)

  (gimp-image-undo-enable image)
  (gimp-display-new image)))

(script-fu-register "script-fu-colorcycling-anim"
		    _"<Image>/Script-Fu/Animators/Color Cycling..."
		    "Creates an animation with the help of the alienmap plug-in"
		    "Daniel Cotting (cotting@mygale.org)"
		    "Daniel Cotting"
		    "December 1997"
		    "RGB*"
		    SF-IMAGE "Image" 0
		    SF-DRAWABLE "Drawable" 0
		    SF-ADJUSTMENT _"Number of Frames" '(10 2 100 1 10 0 1)
                    SF-ADJUSTMENT _"Start: Red Intensity Factor" '(128 0 128 1 10 0 1)
                    SF-ADJUSTMENT _"Start: Green Intensity Factor" '(128 0 128 1 10 0 1)
                    SF-ADJUSTMENT _"Start: Blue Intensity Factor" '(128 0 128 1 10 0 1)
                    SF-ADJUSTMENT _"End: Red Intensity Factor" '(128 0 128 1 10 0 1)
                    SF-ADJUSTMENT _"End: Green Intensity Factor" '(128 0 128 1 10 0 1)
                    SF-ADJUSTMENT _"End: Blue Intensity Factor" '(128 0 128 1 10 0 1)
                    SF-ADJUSTMENT _"Red Color Mode (sin:0/cos:1/none:2)" '(0 0 2 1 1 0 1)
                    SF-ADJUSTMENT _"Green Color Mode (sin:0/cos:1/none:2)" '(0 0 2 1 1 0 1)
                    SF-ADJUSTMENT _"Blue Color Mode (sin:0/cos:1/none:2)" '(0 0 2 1 1 0 1)
                    SF-TOGGLE _"Red Inversion before Transformation" FALSE
                    SF-TOGGLE _"Green Inversion before Transformation" FALSE
                    SF-TOGGLE _"Blue Inversion before Transformation" FALSE
                    SF-ADJUSTMENT _"Start: Red Phase Displacement (RAD)" '(0 0 6.28 0.05 1 2 1)
                    SF-ADJUSTMENT _"Start: Green Phase Displacement (RAD)" '(0 0 6.28 0.05 1 2 1)
                    SF-ADJUSTMENT _"Start: Blue Phase Displacement (RAD)" '(0 0 6.28 0.05 1 2 1)
                    SF-ADJUSTMENT _"End: Red Phase Displacement (RAD)" '(0 0 6.28 0.05 1 2 1)
                    SF-ADJUSTMENT _"End: Green Phase Displacement (RAD)" '(0 0 6.28 0.05 1 2 1)
                    SF-ADJUSTMENT _"End: Blue Phase Displacement (RAD)" '(0 0 6.28 0.05 1 2 1)
                    SF-ADJUSTMENT _"Start: Red Frequency (> 0)" '(1 0.01 5 0.05 1 2 1)
                    SF-ADJUSTMENT _"Start: Green Frequency (> 0)" '(1 0.01 5 0.05 1 2 1)
                    SF-ADJUSTMENT _"Start: Blue Frequency (> 0)" '(1 0.01 5 0.05 1 2 1)
                    SF-ADJUSTMENT _"End: Red Frequency (> 0)" '(1 0.01 5 0.05 1 2 1)
                    SF-ADJUSTMENT _"End: Green Frequency (> 0)" '(1 0.01 5 0.05 1 2 1)
                    SF-ADJUSTMENT _"End: Blue Frequency (> 0)" '(1 0.01 5 0.05 1 2 1)
                    SF-TOGGLE _"Red Inversion after Transformation" FALSE
                    SF-TOGGLE _"Green Inversion after Transformation" FALSE
                    SF-TOGGLE _"Blue Inversion after Transformation" FALSE)

