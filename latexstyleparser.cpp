#include "latexstyleparser.h"

LatexStyleParser::LatexStyleParser(QObject *parent,QString baseDirName,QString kpsecmd) :
    SafeThread(parent)
{
    baseDir=baseDirName;
    stopped=false;
    kpseWhichCmd=kpsecmd;
    mFiles.clear();
    //check if pdflatex is present
    texdefDir=kpsecmd.left(kpsecmd.length()-9);
    QProcess myProc(0);
    //myProc.start(texdefDir+"texdef");
    myProc.start(texdefDir+"pdflatex --version");
    myProc.waitForFinished();
    texdefMode=(myProc.exitCode()==0);
    if(texdefMode){
	QString output=myProc.readAllStandardOutput();
	output=output.split("\n").first().trimmed();
	if(output.contains("MiKTeX")){
	    kpseWhichCmd.chop(9);
	    kpseWhichCmd.append("findtexmf");
	}
    }
}

void LatexStyleParser::stop(){
	stopped=true;
	mFilesAvailable.release();
}

void LatexStyleParser::run(){
	forever {
		//wait for enqueued lines
		mFilesAvailable.acquire();
		if(stopped && mFiles.count()==0) break;
		mFilesLock.lock();
		QString fn=mFiles.dequeue();
		mFilesLock.unlock();
        QString topPackage="";
        QString dirName="";
        if(fn.contains("/")){
            int i=fn.indexOf("/");
            if(i>-1){
                dirName=fn.mid(i+1);
                fn=fn.left(i);
            }
        }
        if(fn.contains('#')){
            QStringList lst=fn.split('#');
            if(!lst.isEmpty())
                topPackage=lst.takeFirst();
            if(!lst.isEmpty())
                fn=lst.last();
        }else{
            topPackage=fn;
            topPackage.chop(4);
        }
        QString fullName=kpsewhich(fn); // find file
        if(fullName.isEmpty()){
            if(!dirName.isEmpty()){
                fullName=kpsewhich(fn,dirName); // find file
            }
        }
        if(fullName.isEmpty())
            continue;

        QStringList results,parsedPackages;
        results=readPackage(fullName,parsedPackages); // parse package(s)
        results.sort();

        if(texdefMode){
            QStringList appendList;
            //QStringList texdefResults=readPackageTexDef(fn); // parse package(s) by texdef as well for indirectly defined commands
            QStringList texdefResults=readPackageTracing(fn);
            texdefResults.sort();
            // add only additional commands to results
            if( !results.isEmpty() && !texdefResults.isEmpty()){
                QStringList::const_iterator texdefIterator;
                QStringList::const_iterator resultsIterator=results.constBegin();
                QString result=*resultsIterator;
                for (texdefIterator = texdefResults.constBegin(); texdefIterator != texdefResults.constEnd(); ++texdefIterator){
                    QString td=*texdefIterator;
                    if(td.startsWith('#'))
                        continue;
                    int i=td.indexOf("#");
                    if(i>=0)
                        td=td.left(i);
                    while(result<td && resultsIterator!=results.constEnd()){
                        ++resultsIterator;
                        if(resultsIterator!=results.constEnd()){
                            result=*resultsIterator;
                        }else{
                            result.clear();
                        }
                    }
                    //compare result/td
                    bool addCommand=true;

                    if(result.startsWith(td)){
                        if(result.length()>td.length()){
                            QChar c=result.at(td.length());
                            switch(c.toLatin1()){
                            case '#':;
                            case '{':;
                            case '[':addCommand=false;
                            default:
                                break;
                            }
                        }else{
                            addCommand=false; //exactly equal
                        }
                    }

                    if(addCommand){
                        appendList<<*texdefIterator;
                        //qDebug()<<td;
                    }
                }
            }else{
                if(results.isEmpty())
                    results<<texdefResults;
            }
            results<<appendList;
        }


        // if included styles call for additional generation, do it.
        QStringList included=results.filter(QRegExp("#include:.+"));
        foreach(QString elem,included){
            elem=elem.mid(9);
            QString fn=findResourceFile("completion/"+elem+".cwl",false,QStringList(baseDir));
            if(fn.isEmpty()){
                QString hlp=kpsewhich(elem+".sty");
                if(!hlp.isEmpty()){
                    if(!topPackage.isEmpty())
                        elem=topPackage+"#"+elem+".sty";
                    addFile(elem);
                }
            }
        }

        // write results
        if(!results.isEmpty()){
            QFileInfo info(fn);
            QString baseName=info.completeBaseName();
            if(!baseName.isEmpty()){
                QFile data(baseDir+"/"+baseName+".cwl");
                if(data.open(QFile::WriteOnly|QFile::Truncate)){
                    QTextStream out(&data);
                    out << "# autogenerated by txs\n";
                    foreach(const QString& elem,results){
                        out << elem << "\n";
                    }
                }
                if(!topPackage.isEmpty() && topPackage!=baseName)
                    baseName=topPackage+"#"+baseName;
                emit scanCompleted(baseName);
            }
        }
    }
}

void LatexStyleParser::addFile(QString filename){
	mFilesLock.lock();
	mFiles.enqueue(filename);
	mFilesLock.unlock();
	mFilesAvailable.release();
}

QStringList LatexStyleParser::readPackage(QString fn,QStringList& parsedPackages){
    if(parsedPackages.contains(fn))
	return QStringList();
    QFile data(fn);
    QStringList results;
    if(data.open(QIODevice::ReadOnly | QIODevice::Text)){
        QTextStream stream(&data);
	parsedPackages<<fn;
        QString line;
        QRegExp rxDef("\\\\def\\s*(\\\\[\\w@]+)\\s*(#\\d+)?");
        QRegExp rxLet("\\\\let\\s*(\\\\[\\w@]+)");
        QRegExp rxCom("\\\\(newcommand|providecommand|DeclareRobustCommand)\\*?\\s*\\{(\\\\\\w+)\\}\\s*\\[?(\\d+)?\\]?");
        QRegExp rxCom2("\\\\(newcommand|providecommand|DeclareRobustCommand)\\*?\\s*(\\\\\\w+)\\s*\\[?(\\d+)?\\]?");
        QRegExp rxEnv("\\\\newenvironment\\s*\\{(\\w+)\\}\\s*\\[?(\\d+)?\\]?");
        QRegExp rxInput("\\\\input\\s*\\{?([\\w._]+)");
        QRegExp rxRequire("\\\\RequirePackage\\s*\\{(\\S+)\\}");
        rxRequire.setMinimal(true);
        QRegExp rxRequireStart("\\\\RequirePackage\\s*\\{(.+)");
        QRegExp rxDecMathSym("\\\\DeclareMathSymbol\\s*\\{\\\\(\\w+)\\}");
        QRegExp rxNewLength("\\\\newlength\\s*\\{\\\\(\\w+)\\}");
        QRegExp rxNewCounter("\\\\newcounter\\s*\\{(\\w+)\\}");
        QRegExp rxLoadClass("\\\\LoadClass\\s*\\{(\\w+)\\}");
        bool inReq=false;
        while(!stream.atEnd()) {
            line = stream.readLine();
            line = LatexParser::cutComment(line);
            if(line.startsWith("\\endinput"))
                break;
            int options=0;
            if(inReq){
                int col=line.indexOf('}');
                if(col>-1){
                    QString zw=line.left(col);
                    foreach(QString elem,zw.split(',')){
                        QString package=elem.remove(' ');
                        if(!package.isEmpty())
                            results << "#include:" + package;
                    }
                    inReq=false;
                } else {
                    foreach(QString elem,line.split(',')){
                        QString package=elem.remove(' ');
                        if(!package.isEmpty())
                            results << "#include:" + package;
                    }
                }
                continue;
            }
            if(rxDef.indexIn(line)>-1){
                QString name=rxDef.cap(1);
                if(name.contains("@"))
                    continue;
                QString optionStr=rxDef.cap(2);
                //qDebug()<< name << ":"<< optionStr;
                options=optionStr.mid(1).toInt(); //returns 0 if conversion fails
                for (int j=0; j<options; j++) {
                    name.append(QString("{arg%1}").arg(j+1));
                }
                name.append("#S");
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxLet.indexIn(line)>-1){
                QString name=rxLet.cap(1);
                if(name.contains("@"))
                    continue;
                name.append("#S");
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxCom.indexIn(line)>-1){
                QString name=rxCom.cap(2);
                if(name.contains("@"))
                    continue;
                QString optionStr=rxCom.cap(3);
                //qDebug()<< name << ":"<< optionStr;
                options=optionStr.toInt(); //returns 0 if conversion fails
                for (int j=0; j<options; j++) {
                    name.append(QString("{arg%1}").arg(j+1));
                }
                name.append("#S");
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxCom2.indexIn(line)>-1){
                QString name=rxCom2.cap(2);
                if(name.contains("@"))
                    continue;
                QString optionStr=rxCom2.cap(3);
                //qDebug()<< name << ":"<< optionStr;
                options=optionStr.toInt(); //returns 0 if conversion fails
                for (int j=0; j<options; j++) {
                    name.append(QString("{arg%1}").arg(j+1));
                }
                name.append("#S");
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxEnv.indexIn(line)>-1){
                QString name=rxEnv.cap(1);
                if(name.contains("@"))
                    continue;
                QString optionStr=rxEnv.cap(2);
                //qDebug()<< name << ":"<< optionStr;
                QString zw="\\begin{"+name+"}#S";
                if(!results.contains(zw))
                    results << zw;
                zw="\\end{"+name+"}#S";
                if(!results.contains(zw))
                    results << zw;
                continue;
            }
            if(rxInput.indexIn(line)>-1){
                QString name=rxInput.cap(1);
                name=kpsewhich(name);
                if(!name.isEmpty() && name!=fn) // avoid indefinite loops
		    results << readPackage(name,parsedPackages);
                continue;
            }
            if(rxNewLength.indexIn(line)>-1){
                QString name="\\"+rxNewLength.cap(1);
                if(name.contains("@"))
                    continue;
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxNewCounter.indexIn(line)>-1){
                QString name="\\the"+rxNewCounter.cap(1);
                if(name.contains("@"))
                    continue;
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxDecMathSym.indexIn(line)>-1){
                QString name="\\"+rxDecMathSym.cap(1);
                if(name.contains("@"))
                    continue;
                name.append("#Sm");
                if(!results.contains(name))
                    results << name;
                continue;
            }
            if(rxRequire.indexIn(line)>-1){
                QString arg = rxRequire.cap(1);
                foreach(QString elem,arg.split(',')){
                    QString package=elem.remove(' ');
                    if(!package.isEmpty())
                        results << "#include:" + package;
                }
                continue;
            }
            if(rxRequireStart.indexIn(line)>-1){
                QString arg = rxRequireStart.cap(1);
                foreach(QString elem,arg.split(',')){
                    QString package=elem.remove(' ');
                    if(!package.isEmpty())
                        results << "#include:" + package;
                }
                inReq=true;
            }
            if(rxLoadClass.indexIn(line)>-1){
                QString arg = rxLoadClass.cap(1);
                if(!arg.isEmpty()){
                    if(mPackageAliases.contains(arg))
                        foreach(QString elem,mPackageAliases.values(arg)){
                            results << "#include:" + elem;
                        }
                    else
                        results << "#include:" + arg;
                }
                continue;
            }
        } // while line
    } // open data
    return results;
}

QString LatexStyleParser::kpsewhich(QString name,QString dirName){
    if(name.startsWith("."))
	return "";  // don't check .sty/.cls
    QString fn=name;
    if(!kpseWhichCmd.isEmpty()){
	QProcess myProc(0);
    QStringList options;
    if(!dirName.isEmpty()){
        options<<"-path="+dirName;
    }
    options<<fn;
    myProc.start(kpseWhichCmd,options);
	myProc.waitForFinished();
	if(myProc.exitCode()==0){
	    fn=myProc.readAllStandardOutput();
	    fn=fn.split('\n').first().trimmed(); // in case more than one results are present
	}else
	    fn.clear();
    }
    return fn;
}

QStringList LatexStyleParser::readPackageTexDef(QString fn){
    if(!fn.endsWith(".sty"))
	return QStringList();

    QString fname=fn.left(fn.length()-4);
    QProcess myProc(0);
    //add exec search path
    if(!texdefDir.isEmpty()){
        QStringList env = QProcess::systemEnvironment();
        if(env.contains("SHELL=/bin/bash")){
            env.replaceInStrings(QRegExp("^PATH=(.*)", Qt::CaseInsensitive), "PATH=\\1:"+texdefDir);
            myProc.setEnvironment(env);
        }
    }
    QStringList args;
    QString result;
    args<<"-t"<<"latex" << "-l" << "-p" <<fname;
    myProc.start(texdefDir+"texdef",args);
    args.clear();
    myProc.waitForFinished();
    if(myProc.exitCode()!=0)
    	return QStringList();

    result=myProc.readAllStandardOutput();
    QStringList lines=result.split('\n');

    bool incl=false;
    for(int i=0;i<lines.length();i++){
        if(lines.at(i).startsWith("Defined")){
            QString name=lines.at(i);
            name=name.mid(17);
            name=name.left(name.length()-2);
            incl=(name==fn);
            if(!incl)
                args<<"#include:"+name;
        }
        if(incl && lines.at(i).startsWith("\\")){
            if(lines.at(i).startsWith("\\\\")){
                QString zw=lines.at(i)+"#S";
                zw.remove(0,1);
                //args<<zw;
            }else{
                args<<lines.at(i)+"#S";
            }
        }
    }
    // replace tex env def by latex commands
    QStringList zw=args.filter(QRegExp("\\\\end.+"));
    foreach(const QString& elem,zw){
        QString begin=elem;
        begin.remove(1,3);
        int i=args.indexOf(begin);
        if(i!=-1){
            QString env=begin.mid(1,begin.length()-3);
            args.replace(i,"\\begin{"+env+"}");
            i=args.indexOf(elem);
            args.replace(i,"\\end{"+env+"}");
        }
    }

    return args;
}

QStringList LatexStyleParser::readPackageTracing(QString fn){
    if(!fn.endsWith(".sty"))
    return QStringList();

    fn.chop(4);

    QString tempPath = QDir::tempPath()+QDir::separator()+"."+QDir::separator();
    QTemporaryFile *tf=new QTemporaryFile(tempPath + "XXXXXX.tex");
    if (!tf) return QStringList();
    tf->open();

    QTextStream out(tf);
    out << "\\documentclass{article}\n";
    out << "\\usepackage{filehook}\n";
    out << "\\usepackage{currfile}\n";
    out << "\\AtBeginOfEveryFile{\\message{^^Jentering file \\currfilename ^^J}}\n";
    out << "\\AtEndOfEveryFile{\\message{^^Jleaving file \\currfilename ^^J}}\n";
    out << "\\tracingonline=1\n";
    out << "\\tracingassigns=1\n";
    out << "\\usepackage{"<<fn<<"}\n";
    out << "\\tracingassigns=0\n";
    out << "\\AtBeginOfEveryFile{}\n";
    out << "\\AtEndOfEveryFile{}\n";
    out << "\\begin{document}\n";
    out << "\\end{document}";
    tf->close();


    QProcess myProc(0);
    //add exec search path
    if(!texdefDir.isEmpty()){
        QStringList env = QProcess::systemEnvironment();
        if(env.contains("SHELL=/bin/bash")){
            env.replaceInStrings(QRegExp("^PATH=(.*)", Qt::CaseInsensitive), "PATH=\\1:"+texdefDir);
            myProc.setEnvironment(env);
        }
    }
    QStringList args;
    QString result;
    args<<"-draftmode"<<"-interaction=nonstopmode" << "--disable-installer"<< tf->fileName();
    myProc.setWorkingDirectory(QFileInfo(tf->fileName()).absoluteDir().absolutePath());
    myProc.start(texdefDir+"pdflatex",args);
    args.clear();
    myProc.waitForFinished();
    if(myProc.exitCode()!=0)
        return QStringList();

    result=myProc.readAllStandardOutput();
    QStringList lines=result.split('\n');

    QStack<QString> stack;
    foreach(const QString elem,lines){
        if(elem.startsWith("entering file")){
            QString name=elem.mid(14);
            stack.push(name);
            if(stack.size()==2){ // first level of include
                args<<"#include:"+name;
            }
        }
        if(elem.startsWith("leaving file")){
            stack.pop();
            if(stack.isEmpty())
                break;
        }
        if(stack.size()==1 && elem.endsWith("=undefined}")){
            if(elem.startsWith("{changing ")  && !elem.contains("@")){
                QString zw=elem.mid(10);
                zw.chop(11);
                if(!args.contains(zw+"#S"))
                    args<<zw+"#S";
            }
        }
    }

    // replace tex env def by latex commands
    QStringList zw=args.filter(QRegExp("\\\\end.+"));
    foreach(const QString& elem,zw){
        QString begin=elem;
        begin.remove(1,3);
        int i=args.indexOf(begin);
        if(i!=-1){
            QString env=begin.mid(1,begin.length()-3);
            args.replace(i,"\\begin{"+env+"}");
            i=args.indexOf(elem);
            args.replace(i,"\\end{"+env+"}");
        }
    }

    delete tf;
    return args;
}