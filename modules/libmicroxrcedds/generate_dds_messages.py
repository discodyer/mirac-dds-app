#!/usr/bin/env python3
"""
Micro XRCE-DDS智能生成脚本
功能：
1. 从ROS 2系统自动获取最新IDL文件
2. 按原始包结构生成头文件和源文件
3. 支持消息依赖自动解析
"""

import argparse
import os
import re
import subprocess
import sys
import pathlib
import logging
from collections import defaultdict
from typing import List, Dict, Tuple, Optional

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger('uxr-generator')

class IdlGenerator:
    """IDL生成器配置类"""
    def __init__(self, package: str, msg_type: str, idl_path: pathlib.Path):
        """
        :param package: ROS 2包名 (如'sensor_msgs')
        :param msg_type: 消息类型名 (如'Imu')
        :param idl_path: IDL文件绝对路径
        """
        self.package = package
        self.msg_type = msg_type
        self.idl_path = idl_path
        
        # 生成的文件名 (小写格式，符合ROS 2命名规范)
        self.h_name = f"{self.msg_type.lower()}.h"
        self.c_name = f"{self.msg_type.lower()}.c"
        
        # 生成文件的相对路径
        self.rel_dir = pathlib.Path(self.package) / "msg"
    
    def __repr__(self):
        return f"<IdlGenerator {self.package}/{self.msg_type} at {self.idl_path}>"

def find_ros_share_dir(package: str) -> pathlib.Path:
    """查找ROS 2包的share目录"""
    try:
        result = subprocess.run(
            ['ros2', 'pkg', 'prefix', '--share', package],
            capture_output=True, text=True, check=True
        )
        share_path = result.stdout.strip()
        if not share_path:
            raise ValueError(f"Empty share path for package '{package}'")
        return pathlib.Path(share_path)
    except subprocess.CalledProcessError as e:
        logger.error(f"Package '{package}' not found. Is it installed?")
        logger.debug(f"Error details: {e.stderr}")
        raise
    except Exception as e:
        logger.error(f"Error locating share dir for {package}: {str(e)}")
        raise

def parse_message_list(message_file: pathlib.Path) -> Dict[str, List[str]]:
    """
    解析消息列表文件
    格式: <package>/<msg_type> (每行一个)
    示例:
        sensor_msgs/Imu
        std_msgs/Header
    """
    if not message_file.exists():
        raise FileNotFoundError(f"Message list file not found: {message_file}")
    
    package_msgs = defaultdict(list)
    
    with open(message_file, 'r') as f:
        for line in f:
            line = line.strip()
            # 跳过空行和注释
            if not line or line.startswith('#'):
                continue
            
            # 验证格式: <package>/<msg_type>
            if '/' not in line:
                logger.warning(f"Invalid message format: {line}. Skipping.")
                continue
            
            package, msg_type = line.split('/', 1)
            package = package.strip()
            msg_type = msg_type.strip()
            
            if not package or not msg_type:
                logger.warning(f"Invalid message format: {line}. Skipping.")
                continue
                
            package_msgs[package].append(msg_type)
    
    return package_msgs

def locate_idl_files(package_msgs: Dict[str, List[str]]) -> List[IdlGenerator]:
    """定位所有IDL文件"""
    idl_generators = []
    missing_packages = []
    
    for package, msg_types in package_msgs.items():
        try:
            share_dir = find_ros_share_dir(package)
            msg_dir = share_dir / "msg"
            
            if not msg_dir.exists():
                logger.warning(f"No 'msg' directory found for package {package} at {msg_dir}")
                continue
                
            for msg_type in msg_types:
                idl_path = msg_dir / f"{msg_type}.idl"
                
                if not idl_path.exists():
                    logger.warning(f"IDL file not found: {idl_path}")
                    continue
                    
                idl_generators.append(IdlGenerator(package, msg_type, idl_path))
                
        except Exception as e:
            logger.error(f"Failed to process package {package}: {str(e)}")
            missing_packages.append(package)
    
    if missing_packages:
        logger.error(f"Missing packages: {', '.join(missing_packages)}. Generation may be incomplete.")
    
    return idl_generators

def generate_uxr_code(
    idl_generators: List[IdlGenerator],
    output_dir: pathlib.Path,
    include_paths: List[pathlib.Path] = None,
    replace: bool = True,
    # container_prealloc_size: int = 4
):
    """生成Micro XRCE-DDS代码"""
    # 按包分组
    package_groups = defaultdict(list)
    for gen in idl_generators:
        package_groups[gen.package].append(gen)
    
    # 确保输出目录存在
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # 收集所有生成的源文件路径
    generated_files = []
    
    for package, generators in package_groups.items():
        logger.info(f"Processing package: {package}")
        
        # 为包创建输出目录
        pkg_output_dir = output_dir / package / "msg"
        pkg_output_dir.mkdir(parents=True, exist_ok=True)
        
        # 收集此包的所有IDL文件
        idl_files = [str(gen.idl_path) for gen in generators]
        
        # 构建命令行参数
        cmd = [
            'microxrceddsgen',
            '-cs',  # case sensitive
            '-replace' if replace else '',
            # f'-default-container-prealloc-size={container_prealloc_size}',
            '-d', str(pkg_output_dir),
        ]
        
        # 添加包含路径
        if include_paths:
            for path in include_paths:
                cmd.extend(['-I', str(path)])
        
        # 添加IDL文件
        cmd.extend(idl_files)
        
        # 过滤空参数
        cmd = [arg for arg in cmd if arg]
        
        logger.debug(f"Running command: {' '.join(cmd)}")
        
        try:
            # 执行生成命令
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            logger.debug(f"Generation output for {package}:\n{result.stdout}")
            
            # 记录生成的文件
            for gen in generators:
                h_file = pkg_output_dir / gen.h_name
                c_file = pkg_output_dir / gen.c_name
                
                if h_file.exists() and c_file.exists():
                    logger.info(f"Generated: {h_file.relative_to(output_dir)}")
                    logger.info(f"Generated: {c_file.relative_to(output_dir)}")
                    generated_files.append(h_file)
                    generated_files.append(c_file)
                else:
                    logger.warning(f"Missing generated files for {package}/{gen.msg_type}")
            
        except subprocess.CalledProcessError as e:
            logger.error(f"Failed to generate code for package {package}")
            logger.error(f"Command: {' '.join(e.cmd)}")
            logger.error(f"Error code: {e.returncode}")
            logger.error(f"Stderr:\n{e.stderr}")
        except Exception as e:
            logger.error(f"Unexpected error generating code for {package}: {str(e)}")
    
    return generated_files

def main():
    # 解析命令行参数
    parser = argparse.ArgumentParser(
        description="Micro XRCE-DDS代码生成器 - 保留原始ROS 2包结构",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        'message_file',
        type=pathlib.Path,
        help="包含消息列表的文件（每行格式：<package>/<msg_type>）"
    )
    parser.add_argument(
        '-o', '--output',
        type=pathlib.Path,
        default=pathlib.Path.cwd() / "uxr_generated",
        help="输出目录路径"
    )
    parser.add_argument(
        '-I', '--include',
        type=pathlib.Path,
        nargs='+',
        default=[],
        help="额外的包含目录"
    )
    parser.add_argument(
        '--no-replace',
        action='store_false',
        dest='replace',
        help="不替换现有文件"
    )
    # parser.add_argument(
    #     '--container-prealloc-size',
    #     type=int,
    #     default=4,
    #     help="容器预分配大小"
    # )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help="启用详细输出"
    )
    
    args = parser.parse_args()
    
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    
    # 1. 解析消息列表
    logger.info(f"Parsing message list: {args.message_file}")
    try:
        package_msgs = parse_message_list(args.message_file)
    except Exception as e:
        logger.error(f"Failed to parse message list: {str(e)}")
        sys.exit(1)
    
    if not package_msgs:
        logger.error("No valid messages found in the input file")
        sys.exit(1)
    
    logger.info(f"Found {len(package_msgs)} packages with messages")
    
    # 2. 定位IDL文件
    logger.info("Locating IDL files in ROS 2 system...")
    idl_generators = locate_idl_files(package_msgs)
    
    if not idl_generators:
        logger.error("No valid IDL files found. Exiting.")
        sys.exit(1)
    
    logger.info(f"Found {len(idl_generators)} IDL files for generation")
    
    # 3. 生成代码
    logger.info(f"Generating code to: {args.output}")
    generated_files = generate_uxr_code(
        idl_generators,
        args.output,
        include_paths=args.include,
        replace=args.replace,
        # container_prealloc_size=args.container_prealloc_size
    )
    
    # 4. 输出结果
    logger.info("\nGeneration completed!")
    logger.info(f"Total files generated: {len(generated_files)}")
    logger.info(f"Output directory: {args.output.absolute()}")

if __name__ == "__main__":
    main()