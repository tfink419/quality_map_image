require "bundler/gem_tasks"
require "rspec/core/rake_task"
Rake.add_rakelib 'lib/tasks'

RSpec::Core::RakeTask.new(:spec)

task :default => %w(clean build install spec)