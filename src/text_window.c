/* 
 * Ardesia -- a program for painting on the screen
 * with this program you can play, draw, learn and teach
 * This program has been written such as a freedom sonet
 * We believe in the freedom and in the freedom of education
 *
 * Copyright (C) 2009 Pilolli Pietro <pilolli@fbk.eu>
 *
 * Ardesia is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Ardesia is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Widget for text insertion */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <utils.h>
#include <annotation_window.h>
#include <text_window.h>

#ifdef _WIN32
#include <windows_utils.h>
#endif


static TextData* text_data = NULL;


/* Start the virtual keyboard */
static void start_virtual_keyboard()
{ 
  gchar* argv[2] = {VIRTUALKEYBOARD_NAME, (gchar*) 0};

  g_spawn_async (NULL /*working_directory*/,
		 argv,
		 NULL /*envp*/,
		 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
		 NULL /*child_setup*/,
		 NULL /*user_data*/,
		 &text_data->virtual_keyboard_pid /*child_pid*/,
		 NULL /*error*/);
}


/* Stop the virtual keyboard */
static void stop_virtual_keyboard()
{
  if (text_data->virtual_keyboard_pid > 0)
    { 
      /* @TODO replace this with the cross plattform g_pid_terminate when it will available */
#ifdef _WIN32
      HWND hWnd = FindWindow(VIRTUALKEYBOARD_WINDOW_NAME, NULL);       
      SendMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
#else
      kill (text_data->virtual_keyboard_pid, SIGTERM);
#endif   
      g_spawn_close_pid(text_data->virtual_keyboard_pid); 
      text_data->virtual_keyboard_pid = (GPid) 0;
    }
}


/* Create the text window */
static void create_text_window(GtkWindow *parent)
{
  
  GError* error = NULL;

  if (!text_data->gtk_builder)
    {
      /* Initialize the main window */
      text_data->gtk_builder = gtk_builder_new();

      /* Load the gtk builder file created with glade */
      gtk_builder_add_from_file(text_data->gtk_builder, TEXT_UI_FILE, &error);

      if (error)
	{
	  g_warning ("Couldn't load builder file: %s", error->message);
	  g_error_free (error);
	  return;
	}  
    }


  if (!text_data->window)
    {
      text_data->window = GTK_WIDGET(gtk_builder_get_object(text_data->gtk_builder, "text_window"));   

      gtk_window_set_transient_for(GTK_WINDOW(text_data->window), GTK_WINDOW(parent));

      gtk_window_set_opacity(GTK_WINDOW(text_data->window), 1);
      gtk_widget_set_usize (GTK_WIDGET(text_data->window), gdk_screen_width(), gdk_screen_height());
    }
     
}


/* Move the pen cursor */
static void move_editor_cursor()
{
  if (text_data->cr)
    {
      cairo_move_to(text_data->cr, text_data->pos->x, text_data->pos->y);
    }
}


/* Blink cursor */
static gboolean blink_cursor(gpointer data)
{
  if ((text_data->window)&&(text_data->pos))
    {
      cairo_t* cr = gdk_cairo_create(text_data->window->window);
      cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); 
        
      gint height = text_data->max_font_height;  

      if (text_data->blink_show)
	{
	  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	  cairo_set_line_width(cr, text_data->pen_width);
	  cairo_set_source_color_from_string(cr, text_data->color);
          cairo_rectangle(cr, text_data->pos->x, text_data->pos->y - height, TEXT_CURSOR_WIDTH, height);  
	  text_data->blink_show=FALSE;
	}  
      else
	{
	  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
          cairo_rectangle(cr, text_data->pos->x, text_data->pos->y - height, TEXT_CURSOR_WIDTH, height);  
          cairo_rectangle(cr, text_data->pos->x-1, text_data->pos->y - height - 1, TEXT_CURSOR_WIDTH  + 2, height + 2);  
	  text_data->blink_show=TRUE;
	}  
      cairo_fill(cr);
      cairo_stroke(cr);
      cairo_destroy(cr);
    }
  return TRUE;
}


/* Delete the last character printed */
static void delete_character()
{
  CharInfo *charInfo = (CharInfo *) g_slist_nth_data (text_data->letterlist, 0);

  if (charInfo)
    {  
      cairo_set_operator (text_data->cr, CAIRO_OPERATOR_CLEAR);
 
      cairo_rectangle(text_data->cr, charInfo->x + charInfo->x_bearing -1, 
		      charInfo->y + charInfo->y_bearing -1, 
		      gdk_screen_width()+2, 
		      text_data->max_font_height + 2);

      cairo_fill(text_data->cr);
      cairo_stroke(text_data->cr);
      cairo_set_operator (text_data->cr, CAIRO_OPERATOR_SOURCE);
      text_data->pos->x = charInfo->x;
      text_data->pos->y = charInfo->y;
      text_data->letterlist = g_slist_remove(text_data->letterlist,charInfo); 
    }
}


/* Stop the timer to handle the bloking cursor */
static void stop_timer()
{
  if (text_data->timer>0)
    {
      g_source_remove(text_data->timer); 
      text_data->timer = -1;
    }
}


/* Set the text cursor */
static gboolean set_text_cursor(GtkWidget * window)
{
  gdouble decoration_height = 4;     
  gint height = text_data->max_font_height + decoration_height * 2;
  gint width = TEXT_CURSOR_WIDTH * 3;
  
  GdkPixmap *pixmap = gdk_pixmap_new (NULL, width, height, 1);
  cairo_t *text_pointer_pcr = gdk_cairo_create(pixmap);
  clear_cairo_context(text_pointer_pcr);
  cairo_set_operator(text_pointer_pcr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgb(text_pointer_pcr, 1, 1, 1);
  cairo_paint(text_pointer_pcr);
  cairo_stroke(text_pointer_pcr);
  cairo_destroy(text_pointer_pcr);
  
  GdkPixmap *bitmap = gdk_pixmap_new (NULL, width, height, 1);  
  cairo_t *text_pointer_cr = gdk_cairo_create(bitmap);
  
  if (text_pointer_cr)
    {
      clear_cairo_context(text_pointer_cr);
      cairo_set_source_rgb(text_pointer_cr, 1, 1, 1);
      cairo_set_operator(text_pointer_cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_line_width(text_pointer_cr, 2);

      cairo_line_to(text_pointer_cr, 1, 1);
      cairo_line_to(text_pointer_cr, width-1, 1);
      cairo_line_to(text_pointer_cr, width-1, decoration_height);
      cairo_line_to(text_pointer_cr, 2*width/3+1, decoration_height);
      cairo_line_to(text_pointer_cr, 2*width/3+1, height-decoration_height);
      cairo_line_to(text_pointer_cr, width-1, height-decoration_height);
      cairo_line_to(text_pointer_cr, width-1, height-1);
      cairo_line_to(text_pointer_cr, 1, height-1);
      cairo_line_to(text_pointer_cr, 1, height-decoration_height);
      cairo_line_to(text_pointer_cr, width/3-1, height-decoration_height);
      cairo_line_to(text_pointer_cr, width/3-1, decoration_height);
      cairo_line_to(text_pointer_cr, 1, decoration_height);
      cairo_close_path(text_pointer_cr);
      
      cairo_stroke(text_pointer_cr);
      cairo_destroy(text_pointer_cr);
    } 
 
  GdkColor *background_color_p = rgba_to_gdkcolor(BLACK);
  GdkColor *foreground_color_p = rgba_to_gdkcolor(text_data->color);
  
  GdkCursor* cursor = gdk_cursor_new_from_pixmap (pixmap, bitmap, foreground_color_p, background_color_p, TEXT_CURSOR_WIDTH, height-decoration_height);
  gdk_window_set_cursor (text_data->window->window, cursor);
  gdk_flush ();
  gdk_cursor_unref(cursor);
  g_object_unref (pixmap);
  g_object_unref (bitmap);
  g_free(foreground_color_p);
  g_free(background_color_p);
  
  return TRUE;
}


/* Initialization routine */
static void init_text_widget(GtkWidget *widget)
{
  if (text_data->cr==NULL)
    {
      text_data->cr = gdk_cairo_create(widget->window);
      cairo_set_operator(text_data->cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_line_width(text_data->cr, text_data->pen_width);
      cairo_set_source_color_from_string(text_data->cr, text_data->color);
      cairo_set_font_size (text_data->cr, text_data->pen_width * 2);
      /* This is a trick we must found the maximum height of the font */
      cairo_text_extents (text_data->cr, "|" , &text_data->extents);
      text_data->max_font_height = text_data->extents.height;
      set_text_cursor(widget);
    }
  
#ifndef _WIN32
  /* Instantiate a trasparent pixmap with a black hole upon the bar area to be used as mask */
  GdkBitmap* shape = gdk_pixmap_new(NULL,  gdk_screen_width(), gdk_screen_height(), 1);
  cairo_t* shape_cr = gdk_cairo_create(shape);

  cairo_set_operator(shape_cr,CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (shape_cr, 1, 1, 1, 1);
  cairo_paint(shape_cr);

  GtkWidget* bar= get_bar_window();
  int x,y,width,height;
  gtk_window_get_position(GTK_WINDOW(bar),&x,&y);
  gtk_window_get_size(GTK_WINDOW(bar),&width,&height);

  cairo_set_operator(shape_cr,CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (shape_cr, 0, 0, 0, 0);
  cairo_rectangle(shape_cr, x, y, width, height);
  cairo_fill(shape_cr);	

  gdk_window_input_shape_combine_mask(text_data->window->window,
				      shape,
				      0, 0);
  cairo_destroy(shape_cr);
#else
  grab_pointer(text_data->window, TEXT_MOUSE_EVENTS);
#endif
  
  if (!text_data->pos)
    {
      clear_cairo_context(text_data->cr);
      text_data->pos = g_malloc (sizeof(Pos));
      text_data->pos->x = 0;
      text_data->pos->y = 0;
      move_editor_cursor();
    }
}


/* Add a savepoint with the text */
static void save_text()
{
  if (text_data)
    {
      text_data->blink_show=FALSE;
      blink_cursor(NULL);   
      stop_timer(); 
      if (text_data->letterlist)
	{
	  annotate_push_context(text_data->cr);
	  g_slist_foreach(text_data->letterlist, (GFunc)g_free, NULL);
	  g_slist_free(text_data->letterlist);
	  text_data->letterlist = NULL;
	}
    } 
}


/* Destroy text window */
static void destroy_text_window()
{
  if (text_data->window)
    {
#ifdef _WIN32
      ungrab_pointer(gdk_display_get_default(), text_data->window);
#endif
      if (text_data->window)
	{
	  gtk_widget_destroy(text_data->window);
	  text_data->window = NULL;
	}
    }
}


/* keyboard event snooper */
G_MODULE_EXPORT gboolean
key_snooper(GtkWidget *widget, GdkEventKey *event, gpointer user_data)  
{
  if (event->type != GDK_KEY_PRESS) {
    return TRUE;
  }
  
  text_data->blink_show=FALSE;
  blink_cursor(NULL);   
  stop_timer(); 
 
  if ((event->keyval == GDK_BackSpace) ||
      (event->keyval == GDK_Delete))
    {
      // undo
      delete_character();
    }
  /* is finished the line or the letter is near to the bar window */
  else if ((text_data->pos->x + text_data->extents.x_advance >= gdk_screen_width()) ||
	   (inside_bar_window(text_data->pos->x + text_data->extents.x_advance, text_data->pos->y-text_data->max_font_height/2)) ||
	   (event->keyval == GDK_Return) ||
	   (event->keyval == GDK_ISO_Enter) || 	
	   (event->keyval == GDK_KP_Enter)
	   )
    {
      text_data->pos->x = 0;
      text_data->pos->y +=  text_data->max_font_height;
    }
  /* is the character printable? */
  else if (isprint(event->keyval))
    {
      
      /* The character is printable */
      gchar *utf8 = g_strdup_printf("%c", event->keyval);
      
      CharInfo *charInfo = g_malloc(sizeof (CharInfo));
      charInfo->x = text_data->pos->x;
      charInfo->y = text_data->pos->y; 
      
      cairo_text_extents (text_data->cr, utf8, &text_data->extents);
      cairo_show_text (text_data->cr, utf8); 
      cairo_stroke(text_data->cr);
 
      charInfo->x_bearing = text_data->extents.x_bearing;
      charInfo->y_bearing = text_data->extents.y_bearing;
     
      text_data->letterlist = g_slist_prepend (text_data->letterlist, charInfo);
      
      /* move cursor to the x step */
      text_data->pos->x +=  text_data->extents.x_advance;
      
      g_free(utf8);
    }
  
  move_editor_cursor();
  text_data->blink_show=TRUE;
  blink_cursor(NULL);   
  text_data->timer = g_timeout_add(1000, blink_cursor, NULL);   
  
  return TRUE;
}


/* The windows has been exposed */
G_MODULE_EXPORT gboolean
on_window_text_expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  gint is_fullscreen = gdk_window_get_state (widget->window) & GDK_WINDOW_STATE_FULLSCREEN;
  if (!is_fullscreen)
    {
      return TRUE;
    }
  if (widget)
    {
      init_text_widget(widget);
    }
  return TRUE;
}


#ifdef _WIN32
static gboolean is_above_virtual_keyboard(gint x, gint y)
{
  RECT rect;
  HWND hWnd = FindWindow(VIRTUALKEYBOARD_WINDOW_NAME, NULL);
  if (!hWnd)
    {
      return FALSE;
    }  
  if (!GetWindowRect(hWnd, &rect))
    { 
      return FALSE;
    }
  if ((rect.left<x)&&(x<rect.right)&&(rect.top<y)&&(y<rect.bottom))
    {
      return TRUE;
    }
  return FALSE;
}
#endif

  
/* This is called when the button is lease */
G_MODULE_EXPORT gboolean
on_window_text_button_release (GtkWidget *win,
			       GdkEventButton *ev, 
			       gpointer user_data)
{
  /* only button1 allowed */
  if (!(ev->button == 1))
    {
      return TRUE;
    }
#ifdef _WIN32
  gboolean above = is_above_virtual_keyboard(ev->x, ev->y);
  if (above)
    {
      /* You have lost the focus; re get it */
      grab_pointer(text_data->window, TEXT_MOUSE_EVENTS);
      /* ignore the data; the event wil be passed to the virtual keyboard */
      return TRUE;
    }
#endif

  if ((text_data) && (text_data->pos))
    {
      save_text();
  
      text_data->pos->x = ev->x;
      text_data->pos->y = ev->y;
      move_editor_cursor();

      stop_virtual_keyboard();
      start_virtual_keyboard();
  
      /* This present the ardesia bar and the panels */
      gtk_window_present(GTK_WINDOW(get_bar_window()));

      gtk_window_present(GTK_WINDOW(text_data->window));
      gdk_window_raise(text_data->window->window);

      text_data->timer = g_timeout_add(1000, blink_cursor, NULL);    
    }

  return TRUE;
}


/* This shots when the text ponter is moving */
G_MODULE_EXPORT gboolean
on_window_text_cursor_motion(GtkWidget *win, 
			     GdkEventMotion *ev, 
			     gpointer func_data)
{
#ifdef _WIN32
  if (inside_bar_window(ev->x, ev->y))
    {
      stop_text_widget();
    }
#endif
  return TRUE;
}


/* Start the widget for the text insertion */
void start_text_widget(GtkWindow *parent, gchar* color, gint tickness)
{
  text_data = g_malloc(sizeof(TextData));

  text_data->gtk_builder = NULL;
  text_data->window = NULL;
  text_data->cr = NULL;
  text_data->pos = NULL;
  text_data->letterlist = NULL; 
  
  text_data->virtual_keyboard_pid = (GPid) 0;
  text_data->snooper_handler_id = 0;
  text_data->timer = -1;
  text_data->blink_show = TRUE;
 
  text_data->color =  color;
  text_data->pen_width = tickness;

  create_text_window(parent);
  
  gtk_window_set_keep_above(GTK_WINDOW(text_data->window), TRUE);

  /* connect all the callback from gtkbuilder xml file */
  gtk_builder_connect_signals(text_data->gtk_builder, (gpointer) text_data); 

  /* install a key snooper */
  text_data->snooper_handler_id = gtk_key_snooper_install(key_snooper, NULL);

  /* This put the window in fullscreen generating an exposure */
  gtk_window_fullscreen(GTK_WINDOW(text_data->window));
 
  gtk_widget_show_all(text_data->window);  
#ifdef _WIN32 
  /* in the gtk 2.16.6 the gtkbuilder property GtkWindow.double-buffered doesn't exist and then I set this by hands */
  gtk_widget_set_double_buffered(text_data->window, FALSE); 
  // I use a layered window that use the black as transparent color
  setLayeredGdkWindowAttributes(text_data->window->window, RGB(0,0,0), 0, LWA_COLORKEY);	
#endif
}


/* Stop the text insertion widget */
void stop_text_widget()
{
  if (text_data)
    {
      stop_virtual_keyboard();
      if (text_data->snooper_handler_id)
	{
	  gtk_key_snooper_remove(text_data->snooper_handler_id);
	  text_data->snooper_handler_id = 0;
	}
      save_text();
      destroy_text_window();
      if (text_data->cr)
	{
	  cairo_destroy(text_data->cr);     
	  text_data->cr = NULL;
	}
      if (text_data->pos)
	{
	  g_free(text_data->pos);
	  text_data->pos = NULL;
	}
      /* unref gtkbuilder */
      if (text_data->gtk_builder)
	{
	  g_object_unref(text_data->gtk_builder);
          text_data->gtk_builder = NULL;
	}
      g_free(text_data);
      text_data = NULL;
    }
}

