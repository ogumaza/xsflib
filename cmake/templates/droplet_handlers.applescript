-- SPDX-License-Identifier: MIT

-- The handlers that the droplet scripts share. CMake appends this file to a droplet's script (xsflib_add_droplet).

-- Shows the results of a run in a dialog with the title appTitle. results is {report text, output folders}. With output
-- folders, the dialog offers to open them.
on showResults(results, appTitle)
	set {reportText, folders} to results
	if (count of folders) is 0 then
		display dialog reportText buttons {"OK"} default button 1 with title appTitle with icon caution
	else
		if (count of folders) is 1 then
			set openLabel to "Open Folder"
		else
			set openLabel to "Open Folders"
		end if
		set answer to display dialog reportText buttons {"OK", openLabel} default button 2 with title appTitle
		if button returned of answer is openLabel then
			repeat with d in folders
				do shell script "open " & quoted form of (d as text)
			end repeat
		end if
	end if
end showResults

-- Returns the name of the file or folder at the POSIX path p.
on lastPathComponent(p)
	-- A folder's POSIX path ends in a slash. The tools name the folder without it.
	if p ends with "/" and (length of p) > 1 then set p to text 1 thru -2 of p
	set saved to AppleScript's text item delimiters
	set AppleScript's text item delimiters to "/"
	set parts to text items of p
	set AppleScript's text item delimiters to saved
	return item -1 of parts
end lastPathComponent

-- Returns the last line of t that isn't empty, or t when every line is empty.
on lastLine(t)
	set theLines to paragraphs of t
	repeat with i from (count of theLines) to 1 by -1
		if (item i of theLines) is not "" then return item i of theLines
	end repeat
	return t
end lastLine

-- Returns the items of theList as text, one line for each item.
on joinLines(theList)
	set saved to AppleScript's text item delimiters
	set AppleScript's text item delimiters to return
	set joined to theList as text
	set AppleScript's text item delimiters to saved
	return joined
end joinLines
